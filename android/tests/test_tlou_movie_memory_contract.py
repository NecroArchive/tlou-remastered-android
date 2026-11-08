"""Avoid the 01.00 devkit-memory crash and duplicate movie-frame writes.

Uses the production source paths; actual decoding is additionally covered by
the existing native Videodec fixture on the build workflow.
"""
import pathlib, unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
ACT=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/GameActivity.java").read_text()
VID=(ROOT/"gow3_pc/src/runtime_videodec.c").read_text()
MEM=(ROOT/"gow3_pc/src/runtime_memory.c").read_text()
SCALE=(ROOT/"gow3_pc/gpu/shim/gow3_movie.cpp").read_text()

class MovieAndMemoryGuards(unittest.TestCase):
    def test_retail_memory_report_is_unconditional_for_100_and_111(self):
        self.assertEqual(ACT.count('env.put("GOW3_DMEM_REPORT_MB","4608");'),1)
        self.assertLess(ACT.index('env.put("GOW3_DMEM_REPORT_MB","4608");'),ACT.index('if("01.11".equals(gameVersion)){'))
        self.assertIn('GOW3_DMEM_REPORT_MB',MEM)
        self.assertIn('The Last of Us',MEM)
        self.assertIn('768 MiB "Game Debug"',MEM)
        self.assertIn('if("01.11".equals(gameVersion)){',ACT)
        self.assertIn('env.put("TLOU_MOVIE_111_LAYOUT","1");',ACT)

    def test_conversion_before_guest_write_with_native_fallback(self):
        picture=VID[VID.index('static int32_t picture('):VID.index('static int32_t receive(')]
        self.assertLess(picture.index('const int scaled=gow3_movie_scale_nv12('),
                        picture.index('if (scaled) {'))
        self.assertLess(picture.index('if (scaled) {'),picture.index('} else {'))
        native=picture[picture.index('} else {'):picture.index('    // The original video frame span')]
        self.assertIn('memcpy(luma+',native)
        self.assertIn('memcpy(chroma+',native)
        self.assertNotIn('memcpy(luma+',picture[:picture.index('if (scaled) {')])
        self.assertIn('if ((uint64_t)pitch*rows*3/2>buffer->frame_buffer_size)',picture)
        self.assertIn('info->frame_pitch=pitch;',picture)
        self.assertIn('info->frame_height=rows;',picture)
        self.assertIn('memory->InvalidateMemory(address, bytes);',SCALE)
        self.assertIn('Gow3Movie::RecordFrame(',SCALE)
        self.assertIn('if (processed != int(height)) return 0;',SCALE)

if __name__=='__main__':
    unittest.main()
