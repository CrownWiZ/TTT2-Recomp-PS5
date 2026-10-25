package org.ttt2.setup;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.AtomicFile;
import android.view.WindowManager;
import android.widget.*;
import java.io.*;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;

/** First-run importer. All large-file work happens off the UI thread, using seekable SAF FDs. */
public final class SetupActivity extends Activity {
    static { System.loadLibrary("ttt2_setup"); }
    private static native String extract(int iso, int tu, String target, Job callback);
    private static native String prepareShaders(String root, Job callback);
    private static final Job job = new Job();
    private Uri iso, tu;
    private TextView message;
    private Button isoButton, tuButton, installButton, playButton, cancelButton, shaderButton;
    private CheckBox debugHudToggle;
    private ProgressBar bar;
    private File root;

    public static final class Job {
        volatile boolean running, cancelled, shaderJob;
        volatile String shaderResult = "";
        volatile int percent;
        volatile String message = "Select your Xbox 360 ISO and Title Update 3 (version 0.0.3.2).\n\nSetup needs about 13 GB of free space in addition to those files. Use local storage or a USB drive. Keep setup open while it extracts.";
        volatile WeakReference<SetupActivity> activity = new WeakReference<>(null);
        // Called on the extraction thread through JNI.
        public boolean progress(int value, String text) {
            percent = value; message = text;
            refresh(); return !cancelled;
        }
        void refresh() {
            SetupActivity a = activity.get();
            if (a != null) a.runOnUiThread(a::refresh);
        }
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        root = getExternalFilesDir(null);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);
        ScrollView scroll = new ScrollView(this); scroll.addView(layout); setContentView(scroll);
        TextView title = new TextView(this); title.setText("Tekken Tag Tournament 2 — Setup"); title.setTextSize(24); layout.addView(title);
        message = new TextView(this); message.setTextSize(17); layout.addView(message);
        isoButton = button(layout, "Choose Xbox 360 ISO", () -> pick(1));
        tuButton = button(layout, "Choose Title Update 3", () -> pick(2));
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); bar.setMax(100); layout.addView(bar);
        installButton = button(layout, "Extract game files", this::install);
        shaderButton = button(layout, "Prepare shaders", this::prepare);
        cancelButton = button(layout, "Cancel", this::cancel);
        debugHudToggle = new CheckBox(this);
        debugHudToggle.setText("Show debug HUD");
        debugHudToggle.setChecked(readDebugHud(root));
        debugHudToggle.setOnClickListener(v -> {
            boolean enabled = debugHudToggle.isChecked();
            try { writeDebugHud(root, enabled); }
            catch (IOException e) {
                debugHudToggle.setChecked(!enabled);
                Toast.makeText(this, "Cannot save debug HUD setting: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        });
        layout.addView(debugHudToggle);
        playButton = button(layout, "Start game", () -> {
            try {
                if (!hasGameSession()) defaults(root);
                startActivity(new Intent(this, org.libsdl.app.SDLActivity.class));
            }
            catch (Exception e) { job.message = "Cannot start: " + e.getMessage(); refresh(); }
        });
        if (state != null) {
            String i = state.getString("iso"), t = state.getString("tu");
            if (i != null) iso = Uri.parse(i);
            if (t != null) tu = Uri.parse(t);
        }
        job.activity = new WeakReference<>(this); refresh();
    }
    private Button button(LinearLayout layout, String label, Runnable action) {
        Button b = new Button(this); b.setText(label); b.setOnClickListener(v -> action.run()); layout.addView(b); return b;
    }
    @Override protected void onResume() { super.onResume(); job.activity = new WeakReference<>(this); refresh(); }
    @Override protected void onDestroy() {
        if (job.activity.get() == this) job.activity = new WeakReference<>(null);
        super.onDestroy();
    }
    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        if (iso != null) state.putString("iso", iso.toString());
        if (tu != null) state.putString("tu", tu.toString());
    }
    @Override public void onBackPressed() {
        if (job.running) cancel();
        else super.onBackPressed();
    }
    private void pick(int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
        intent.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
            Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload"));
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, request);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try { getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
        catch (SecurityException ignored) { /* Descriptor access for this session may still work. */ }
        if (request == 1) iso = uri; else if (request == 2) tu = uri;
        refresh();
    }
    private String name(Uri uri) {
        if (uri == null) return "";
        try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        } catch (Exception ignored) { }
        return "Selected file";
    }
    private static boolean ready(File root) {
        if (root == null) return false;
        File installed = new File(root, "installed-game");
        if (installed.exists()) {
            try (BufferedReader reader = new BufferedReader(new FileReader(new File(installed, "installed.txt")))) {
                int count = 0; String line;
                while ((line = reader.readLine()) != null) {
                    String[] fields = line.split(" ", 2);
                    if (fields.length != 2 || new File(installed, fields[1]).length() != Long.parseLong(fields[0])) return false;
                    ++count;
                }
                return count == 48;
            } catch (Exception e) { return false; }
        }
        // Existing development installs keep their original location and settings.
        return new File(root, "tekken.img").length() == 18612224
            && new File(root, "game_files_x/nrfo.bin").isFile()
            && new File(root, "game_files_x/data000.bin").isFile()
            && new File(root, "game_files_x/update/data100.bin").length() == 7411280;
    }
    private static boolean hasGameSession() {
        Activity game = org.libsdl.app.SDLActivity.getContext();
        return game instanceof org.libsdl.app.SDLActivity
            && !game.isFinishing() && !game.isDestroyed();
    }
    private void refresh() {
        boolean usable = ready(root);
        boolean resume = hasGameSession();
        if (resume && !job.running) message.setText("Your game session is still open. Resume to continue where you left off.");
        else if (root == null) message.setText("App storage is unavailable. Reopen setup after storage is mounted.");
        else if (usable && !job.running) message.setText("Game data is ready. Prepare shaders to reduce first-use stalls, then connect a controller and start the game.\n\nPreparation uses the CPU and may take several minutes. Keep setup open. GPU pipelines are still prepared as scenes appear. You may delete the source ISO and title update after successful setup." + (job.shaderResult.isEmpty() ? "" : "\n\n" + job.shaderResult));
        else message.setText(job.message + (job.running ? "\n" + job.percent + "%" : ""));
        isoButton.setEnabled(!job.running && !usable);
        tuButton.setEnabled(!job.running && !usable);
        isoButton.setText(iso == null ? "Choose Xbox 360 ISO" : "ISO: " + name(iso));
        tuButton.setText(tu == null ? "Choose Title Update 3" : "Update: " + name(tu));
        installButton.setEnabled(root != null && !job.running && !usable && iso != null && tu != null);
        cancelButton.setEnabled(job.running && !job.cancelled);
        playButton.setText(resume ? "Resume game" : "Start game");
        playButton.setEnabled((resume || usable) && !job.running);
        shaderButton.setEnabled(usable && !job.running);
        debugHudToggle.setEnabled(root != null && !job.running);
        bar.setProgress(job.percent);
        if (job.running) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    private static boolean readDebugHud(File root) {
        if (root == null) return false;
        AtomicFile setting = new AtomicFile(new File(root, "config/debug_hud.txt"));
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(setting.openRead(), StandardCharsets.UTF_8))) {
            return "1".equals(reader.readLine());
        } catch (IOException e) { return false; }
    }
    private static void writeDebugHud(File root, boolean enabled) throws IOException {
        if (root == null) throw new IOException("App storage is unavailable");
        File config = new File(root, "config");
        if (!config.isDirectory() && !config.mkdirs()) throw new IOException("Cannot create settings");
        AtomicFile setting = new AtomicFile(new File(config, "debug_hud.txt"));
        FileOutputStream out = null;
        try {
            out = setting.startWrite();
            out.write((enabled ? "1\n" : "0\n").getBytes(StandardCharsets.UTF_8));
            setting.finishWrite(out);
        } catch (IOException e) {
            setting.failWrite(out);
            throw e;
        }
    }
    private static void defaults(File root) throws IOException {
        File config = new File(root, "config");
        if (!config.isDirectory() && !config.mkdirs()) throw new IOException("Cannot create settings");
        String[][] defaults = {{"geometry_mode.txt", "196608\n"}, {"video_hz.txt", "60\n"}};
        for (String[] setting : defaults) {
            File file = new File(config, setting[0]);
            if (file.createNewFile()) try (FileOutputStream out = new FileOutputStream(file)) { out.write(setting[1].getBytes(StandardCharsets.UTF_8)); }
        }
    }
    private static void removeTemporary(File file) throws IOException {
        // Only app-owned install staging directories; never saves/config or completed game files.
        if (java.nio.file.Files.isSymbolicLink(file.toPath())) { java.nio.file.Files.delete(file.toPath()); return; }
        File[] children = file.listFiles();
        if (children != null) for (File child : children) removeTemporary(child);
        if (!file.delete()) throw new IOException("Cannot remove interrupted setup files");
    }
    private void cancel() {
        job.cancelled = true;
        job.message = job.shaderJob ? "Cancelling after current shaders finish…" : "Cancelling and removing temporary files…";
        refresh();
    }
    private void prepare() {
        if (job.running || !ready(root)) return;
        final String path = root.getAbsolutePath();
        job.shaderJob = true; job.cancelled = false; job.percent = 0;
        job.message = "Opening shader compiler…"; job.running = true; refresh();
        new Thread(() -> {
            try {
                System.loadLibrary("ttt2_shader_setup");
                String error = prepareShaders(path, job);
                job.shaderResult = error == null ? job.message : "Shader preparation stopped: " + error;
            } catch (Exception | LinkageError e) {
                job.shaderResult = "Shader preparation stopped: " + e.getMessage();
            } finally { job.running = false; job.refresh(); }
        }, "Shader preparation").start();
    }
    private void install() {
        if (job.running || iso == null || tu == null || root == null) return;
        final Uri inputIso = iso, inputTu = tu;
        final File data = root;
        final android.content.ContentResolver resolver = getApplicationContext().getContentResolver();
        job.shaderJob = false; job.running = true; job.cancelled = false; job.percent = 0; job.message = "Opening files…"; refresh();
        new Thread(() -> {
            try {
                File[] old = data.listFiles(f -> f.getName().startsWith(".ttt2-install-"));
                if (old != null) for (File dir : old) removeTemporary(dir);
                try (ParcelFileDescriptor i = resolver.openFileDescriptor(inputIso, "r");
                     ParcelFileDescriptor t = resolver.openFileDescriptor(inputTu, "r")) {
                    if (i == null || t == null) throw new IOException("Cannot open selected files");
                    String error = extract(i.getFd(), t.getFd(), new File(data, "installed-game").getAbsolutePath(), job);
                    if (error != null) throw new IOException(error);
                }
                defaults(data);
                job.message = "Game data is ready.";
            } catch (Exception e) { job.message = "Setup stopped: " + e.getMessage(); }
            finally { job.running = false; job.refresh(); }
        }, "Game file setup").start();
    }
}
