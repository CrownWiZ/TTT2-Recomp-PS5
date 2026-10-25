#include "../audio_filter_native.h"

extern "C" PPC_FUNC(__imp__sub_82D0DB08);

PPC_FUNC(sub_82D0DB08) {
  if (!ttt2_audio::RunCombFilter(ctx, base))
    __imp__sub_82D0DB08(ctx, base);
}
