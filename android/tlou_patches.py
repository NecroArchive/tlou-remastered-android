"""Verified CUSA00552 executable profiles; every change still requires original bytes.

The eboot with entry 0x56d9f0 was supplied by the user on 2026-11-03.
Its profile is selected by complete SELF SHA-256, not an assumed APP_VER.
No player binary or game content is included here.
"""
import hashlib
import struct

BASE_SELF_SHA256 = "e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7"


def rip_instruction(prefix, address, target):
    return (bytes.fromhex(prefix) + struct.pack("<i", target - address - 8)).hex()


BASE_PATCHES = {
    # Missing third component-transform source: the observed crash dereferences
    # NULL+0x20 at 0xf6beb0. Return before the prologue only for absent source;
    # valid input executes the original displaced prologue and original body.
    "null-component-transforms": [
        (0xF6BE63, "90"*13, "4885c974fa554889e54157eb06"),
        (0xF6BE70, "554889e54157", "e9eeffffff90"),
        (0xF6BE54, "4883c4185b415c415d415e415f5dc3", None),
        (0xF6BE76, "415641554154534883ec184889cb4989d6", None),
        (0xF6BEB0, "8b3383feff4c89f8", None),
        (0x92C942, "488b5330488b8a800000004883c260", None),
    ],
    # Static ray-hit processing lacks the invalid-submesh skip already present
    # in the dynamic-mesh path at 0xF753BE. Reject an unavailable/static mesh's
    # hit before computing its submesh address; continue with the next result.
    # Keep every valid hit on the original path. Do not suppress global asserts.
    "static-ray-hit-submesh-bounds": [
        (0xF7516C, "0f849e020000", "0f8e9e020000"),
        (0xF7517F, "39c87f33e81865b3ff", "39c87733e988020000"),
        (0xF7512F, "438b8435d80d00004c69e0f0030000", None),
        (0xF75162, "428b8423f002000085c0", None),
        (0xF75172, "4b638c35dc0d000048894c2418", None),
        (0xF751B6, "4869442418900300004a038423f8020000", None),
        (0xF753BE, "39c3734e", None),
        (0xF75410, "488b5c242848ffc3413b5d100f8cbefbffff", None),
        (0xF7665F, "e85ce8ffff", None),
    ],
    # WaitOnContextDone's compare and diagnostic both read 20 seconds rather than
    # 2. Reuse existing immutable constants; do not alter a shared constant pool.
    "frame-timeout": [
        (0x99285A, "c5f82e05b6e47900", rip_instruction("c5f82e05", 0x99285A, 0xEFDD88)),
        (0x992907, "c5fb100511e47900", rip_instruction("c5fb1005", 0x992907, 0x107CB00)),
        (0xEFDD88, "0000a041", None),
        (0x107CB00, "0000000000003440", None),
        (0x992862, "775a", None),  # retains the real timeout failure branch
    ],
    # Worker spin -> existing condition-variable wait after 1000 iterations;
    # keep the submit path broadcasting that same condition variable.
    "job-sleep": [
        (0xAACA3A, "7504", "eb04"),
        (0xAACA65, "7422", "9090"),
        (0xAABF91, "742d", "9090"),
        (0xAACA36, "807f5900", None),
        (0xAACA40, "4981f8e8030000", None),
        (0xAACA61, "80785900", None),
        (0xAACA75, "ba0a000000", None),
        (0xAABF8D, "807f5900", None),
        (0xAABFAF, "e86ca6ffff", None),
    ],
}


# Profiles validated against the original 24,220,371-byte user-provided SELF files.
# The second hash is an independently patched variant; file size is NOT an identity.
SELF_SHA256_111 = {
    "3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e",
    "31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7",
}
KNOWN_SELF_SHA256 = frozenset({BASE_SELF_SHA256} | SELF_SHA256_111)

BASE_PATCHES_111 = {
    "frame-timeout": [
        (0xB3D887, "c5f82e05819f6300", "c5f82e05194d5c00"),
        (0xB3D92F, "c5fb1005f19e6300", "c5fb1005a13d5900"),
        (0x11025A8, "0000a041", None),
        (0x10D16D8, "0000000000003440", None),
    ],
    "job-sleep": [
        (0xC87D63, "7304", "eb04"),
        (0xC87D96, "741b", "9090"),
        (0xC873E1, "742d", "9090"),
    ],
    # 01.11 guest SIGSEGV at 0xA623F5 (vblendps [r14]) in the animation
    # update loop. r14 was 0x100000007: a non-guest-memory sentinel stored
    # in the output-node pointer slot at [r15+rbx-0x68]. Redirect its 5-byte
    # load to a verified 31-byte NOP cave, then skip only entries with pointers
    # below the mapped guest arena (0x1000000000). The valid path returns with
    # the original load and all other registers/stack preserved. Skip target
    # 0xA625B0 restores rdi before the loop increment. Both 01.11 SELF hashes
    # were checked against these exact original bytes; 01.00 is not changed.
    "invalid-animation-output-pointer": [
        (0xA623D3, "4d8b741f98", "e889d82300"),
        (0xC9FC61, "90" * 31,
         "4d8b741f98504c89f048c1e8244885c05875094883c408"
         "e93329dcffc39090"),
    ],
}
# Original-byte guards checked independently in BOTH supplied 01.11 ELFs.
# In 01.11 the graphics settings are reconstructed from game/config data after
# the constructor runs. Defaults alone can therefore be overwritten. Keep the
# guarded defaults AND disable the relevant render consumers so that the effect
# stays off even when a saved setting (re)enables the flag.
#
# 2026-11-08: checked every added original instruction against both user-owned
# 01.11 SELF profiles (3cfa475f... and 31f3b0d1...). Consumer branches target
# the same "effect off" block as their original JZ/JE, not arbitrary code.
# Bloom's "xor al,al" preserves the three adjacent option bytes (including
# tonemapping) rather than zeroing the packed four-byte configuration word.
PERFORMANCE_EFFECTS_111 = {
    "ssao": [
        (0xAEB0CC, "c7834b01000001010101", "c7834b01000000010101"),
        # Render consumer: skip ambient-occlusion contribution irrespective
        # of config reloads. JE 0xF911 -> JMP 0xF911.
        (0xF907, "7408", "eb08"),
    ],
    "motion_blur": [
        (0xAEB14B, "c6836d01000001", "c6836d01000000"),
        (0x758691, "c6830e04000001", "c6830e04000000"),
        # Motion reconstruction and postprocessing use the reloaded option.
        (0xACB868, "0f849a090000", "e99b09000090"),
        (0xAE09C0, "0f8485030000", "e98603000090"),
        (0xAE0D7F, "7418", "eb18"),
        (0xAE0DD6, "0f84fe000000", "e9ff00000090"),
        (0xAE0F25, "0f8451010000", "e95201000090"),
        # r15d is zero at 0xB87801; replace conditional move with the
        # original disabled-value move while preserving flags.
        (0xB8781E, "410f44c7", "4489f890"),
        # The 01.11 runtime setter at 0x193B50 rewrites bytes
        # [option+0x16C..0x16D] to 01,01 after an initially disabled
        # option is loaded. Fix the stored second byte AND the getter:
        # otherwise later config loads reactivate blur despite all render
        # consumers being patched. Both original SELF variants match.
        (0x193B6A, "66c7836c0100000101", "66c7836c0100000100"),
        (0x193BB0, "8a876d010000", "31c090909090"),
    ],
    "depth_of_field": [
        (0xAEB167, "c6837101000001", "c6837101000000"),
        # The bool getter loads AL and tests it; XOR AL,AL returns false
        # without changing neighboring settings.
        (0xA86FFE, "84c0", "30c0"),
        (0xB87611, "740d", "eb0d"),
        (0xB88604, "742b", "eb2b"),
    ],
    "bloom": [
        (0xAEB202, "c783b401000001010101", "c783b401000001000101"),
        # Packed flags at +0x1B4: bit/byte zero is "Use Bloom". Clear only
        # AL at consumption, retaining "Use Tonemap" and subsequent flags.
        (0xA86F8B, "84c0", "30c0"),
    ],
    "sunlight_shadows": [
        (0xA905B5, "0f85f6010000", "e9f701000090"),
        (0xA905AE, "80b89c000000000f85f6010000", None),
    ],
    "subsurface": [
        (0xA9250A, "0f840c060000", "e90d06000090"),
        (0xA92500, "488b0080b8942e0000000f840c060000", None),
    ],
    # Optional 01.11 light-culling overrides, OFF by default. The guest
    # distinguishes each shadow-casting light category from the corresponding
    # non-shadow category. Force only the shadow flag read to false; preserve
    # the adjacent non-shadow lighting flag and the original skip branch.
    # Original bytes verified on BOTH supplied 01.11 SELF hashes. The source
    # render-state base at 0xA638C4 is 0x181A770, not a texture/global shader.
    "omni_light_shadows": [
        (0xA638C4, "4c8d25a56edb00", None),  # lea r12,[rip+render-state]
        (0xA63BBA, "418a84244d010000", "30c0" + "90"*6),  # shadow category +0x14d
        (0xA63BCA, "418a84244f010000", None),  # PRESERVE omni nonshadow +0x14f
    ],
    "spot_light_shadows": [
        (0xA6445F, "418a84244e010000", "30c0" + "90"*6),  # spot shadow +0x14e
        (0xA6446F, "418a842450010000", None),  # PRESERVE spot nonshadow +0x150
        (0xA652A4, "418a84244e010000", "30c0" + "90"*6),  # second spot consumer
        (0xA652B8, "418a842450010000", None),  # PRESERVE spot nonshadow +0x150
    ],
}

def known_profile(digest):
    return "01.00" if digest == BASE_SELF_SHA256 else "01.11" if digest in SELF_SHA256_111 else None

def select(eboot, fallback):
    digest = hashlib.sha256(eboot.read_bytes()).hexdigest()
    if digest == BASE_SELF_SHA256:
        print("Perfil TLOU 01.00 validado por SHA-256.", flush=True)
        return dict(BASE_PATCHES)
    if digest in SELF_SHA256_111:
        print("Perfil TLOU 01.11 validado por SHA-256.", flush=True)
        return dict(BASE_PATCHES_111)
    return fallback

# Adapted from Bachata S4+ P36 performance XMLs. Addresses are ELF-relative
# (Bachata guest base 0x400000 removed). Entire instruction guards were checked
# against the supplied SELF. Effect toggles below retain their full instruction guards.
PERFORMANCE_EFFECTS = {'ssao': [[9724771, '01', '00'], [9724765, 'c7834b01000001010101', None]],
 'motion_blur': [[9724898, '01', '00'],
                 [9725988, '01', '00'],
                 [9724892, 'c6836d01000001', None],
                 [9725982, 'c6830e04000001', None]],
 'depth_of_field': [[9724926, '01', '00'], [9724920, 'c6837101000001', None]],
 'bloom': [[9725092, 'c783b401000001010101', 'c783b401000001000101']],
 'sunlight_shadows': [[9370914, '0f8583030000', 'e98403000090'],
                      [9370900, '488d05d595fa0080b89c000000000f8583030000', None]],
 'subsurface': [[9381008, '0f84320b0000', 'e9330b000090'],
                [9380998, '488b0080b8942e0000000f84320b0000', None]]}
# Complete 32-write profiles from Bachata S4+ 0.2.7:
# patches/bachata-official/TheLastOfUsRemastered.xml (CUSA00556).
# XML addresses minus 0x400000; every original span also matches our CUSA00552 SELF.
# Includes custom setter, saved-config fallback, scene consumers, native output
# and movie viewport/AA separation. The shorter CUSA00552_Performance XML is incomplete.
PERFORMANCE_RESOLUTIONS = {
    "720": [
        (0x910740, "c5f828d889770889570cc747140000000089771889571c4189f0c4c1fa2ac089d2c4e1fa2acac5f259d3c5fa115f20c5ea5ed089d6c1ee024489c0c1e80344894724895728c5fa115f2cc5fa115710894740897744c7476800007042c7476c000000004489473889573c894f74c7475032000000c7475416000000c74758cd040000c7475cb9020000c747489c040000c7474ca4020000c74760ce040000c74764ba020000e946530e00660f1f440000", "be00050000bad0020000c5f828d889770889570cc747140000000089771889571c4189f0c4c17a2ac0c5fa2acac5f259d3c5fa115f20c5ea5ed089d6c1ee02b8a000000044894724895728c5fa115f2cc5fa115710894740897744c7476800007042c7476c000000004489473889573c894f74c7475032000000c7475416000000c74758cd040000c7475cb9020000c747489c040000c7474ca4020000c74760ce040000c74764ba020000e940530e00"),
        (0x9104f2, "83fe030f848400000083fe020f85f6000000c7470800040000c7470c58020000c7471400000000c7471800040000c7471c58020000", "89d1be00050000bad0020000b80000803fc5f96ec0e934020000909090909090909090909090909090909090909090909090909090"),
        (0x9103bb, "80070000", "00050000"),
        (0x9103c2, "38040000", "d0020000"),
        (0x9103d0, "80070000", "00050000"),
        (0x9103d7, "38040000", "d0020000"),
        (0x9103e5, "80070000", "00050000"),
        (0x9103ec, "38040000", "d0020000"),
        (0x910401, "f0000000", "a0000000"),
        (0x910408, "0e010000", "b4000000"),
        (0x91042b, "80070000", "00050000"),
        (0x910432, "38040000", "d0020000"),
        (0x910b22, "80070000", "00050000"),
        (0x910b29, "38040000", "d0020000"),
        (0x910b3e, "80070000", "00050000"),
        (0x910b45, "38040000", "d0020000"),
        (0x910b53, "80070000", "00050000"),
        (0x910b5a, "38040000", "d0020000"),
        (0x910b6f, "f0000000", "a0000000"),
        (0x910b76, "0e010000", "b4000000"),
        (0x910b99, "80070000", "00050000"),
        (0x910ba0, "38040000", "d0020000"),
        (0x91adbc, "488d054dfcef008b5808895d8c448b600c44896588", "bb00050000895d8c41bcd002000044896588909090"),
        (0x92ef1a, "c78500ffffff11000000c744241001000000c744240800000000c70424070000004c89f64489e14189d841b93d000000e8c1d30900", "6a118f8500ffffff6a018f4424106a008f4424086a078f04244c89f6b98007000041b8380400006a3d4159e8c6d309009090909090"),
        (0x92ef7d, "c785f8feffff11000000c744241001000000c744240800000000c7042407000000488d95f8feffff4c89ee4489e14589f041b931000000e857d30900", "6a118f85f8feffff6a018f4424106a008f4424086a078f0424488d95f8feffff4c89eeb9c003000041b81c0200006a314159e85cd309009090909090"),
        (0x92efc0, "c785f0feffff11000000c744241001000000c744240800000000c7042407000000488d95f0feffff4c89ee89d94c8b85d0fcffff41b93d000000e811d30900", "6a118f85f0feffff6a018f4424106a008f4424086a078f0424488d95f0feffff4c89eeb98007000041b8380400006a3d4159e819d309009090909090909090"),
        (0x92f00d, "c785e8feffff11000000c744241001000000c744240800000000c70424070000004c89ee4489e14589f041b931000000e8ced20900", "6a118f85e8feffff6a018f4424106a008f4424086a078f04244c89eeb9c003000041b81c0200006a314159e8d3d209009090909090"),
        (0x91f6e8, "488d0521b3ef00448b7824448b6028", "41bf8007000041bc38040000909090"),
        (0x910580, "470890040000c7470c91020000c747", "be80070000ba38040000e9610e0800"),
        (0x8e441c, "e8cfcf0a00", "e85fc10200"),
        (0x91a37a, "488d058f06f0008b4824c4e1fa2ad18b4028c4e1fa2ad8", "b980070000c4e1fa2ad1b838040000c4e1fa2ad8909090"),
        (0x9d4023, "8b7228c4e1fa2ac6c5fa100da96c7600c5f828d1c5ea5ec08b5224c4e1fa2acac5ea5ec9", "bed6b9723ac5f96ec6ba8988083ac5f96ecac5fa10159f6c760090909090909090909090"),
    ],
    "540": [
        (0x910740, "c5f828d889770889570cc747140000000089771889571c4189f0c4c1fa2ac089d2c4e1fa2acac5f259d3c5fa115f20c5ea5ed089d6c1ee024489c0c1e80344894724895728c5fa115f2cc5fa115710894740897744c7476800007042c7476c000000004489473889573c894f74c7475032000000c7475416000000c74758cd040000c7475cb9020000c747489c040000c7474ca4020000c74760ce040000c74764ba020000e946530e00660f1f440000", "bec0030000ba1c020000c5f828d889770889570cc747140000000089771889571c4189f0c4c17a2ac0c5fa2acac5f259d3c5fa115f20c5ea5ed089d6c1ee02b87800000044894724895728c5fa115f2cc5fa115710894740897744c7476800007042c7476c000000004489473889573c894f74c7475032000000c7475416000000c74758cd040000c7475cb9020000c747489c040000c7474ca4020000c74760ce040000c74764ba020000e940530e00"),
        (0x9104f2, "83fe030f848400000083fe020f85f6000000c7470800040000c7470c58020000c7471400000000c7471800040000c7471c58020000", "89d1bec0030000ba1c020000b80000803fc5f96ec0e934020000909090909090909090909090909090909090909090909090909090"),
        (0x9103bb, "80070000", "c0030000"),
        (0x9103c2, "38040000", "1c020000"),
        (0x9103d0, "80070000", "c0030000"),
        (0x9103d7, "38040000", "1c020000"),
        (0x9103e5, "80070000", "c0030000"),
        (0x9103ec, "38040000", "1c020000"),
        (0x910401, "f0000000", "78000000"),
        (0x910408, "0e010000", "87000000"),
        (0x91042b, "80070000", "c0030000"),
        (0x910432, "38040000", "1c020000"),
        (0x910b22, "80070000", "c0030000"),
        (0x910b29, "38040000", "1c020000"),
        (0x910b3e, "80070000", "c0030000"),
        (0x910b45, "38040000", "1c020000"),
        (0x910b53, "80070000", "c0030000"),
        (0x910b5a, "38040000", "1c020000"),
        (0x910b6f, "f0000000", "78000000"),
        (0x910b76, "0e010000", "87000000"),
        (0x910b99, "80070000", "c0030000"),
        (0x910ba0, "38040000", "1c020000"),
        (0x91adbc, "488d054dfcef008b5808895d8c448b600c44896588", "bbc0030000895d8c41bc1c02000044896588909090"),
        (0x92ef1a, "c78500ffffff11000000c744241001000000c744240800000000c70424070000004c89f64489e14189d841b93d000000e8c1d30900", "6a118f8500ffffff6a018f4424106a008f4424086a078f04244c89f6b98007000041b8380400006a3d4159e8c6d309009090909090"),
        (0x92ef7d, "c785f8feffff11000000c744241001000000c744240800000000c7042407000000488d95f8feffff4c89ee4489e14589f041b931000000e857d30900", "6a118f85f8feffff6a018f4424106a008f4424086a078f0424488d95f8feffff4c89eeb9c003000041b81c0200006a314159e85cd309009090909090"),
        (0x92efc0, "c785f0feffff11000000c744241001000000c744240800000000c7042407000000488d95f0feffff4c89ee89d94c8b85d0fcffff41b93d000000e811d30900", "6a118f85f0feffff6a018f4424106a008f4424086a078f0424488d95f0feffff4c89eeb98007000041b8380400006a3d4159e819d309009090909090909090"),
        (0x92f00d, "c785e8feffff11000000c744241001000000c744240800000000c70424070000004c89ee4489e14589f041b931000000e8ced20900", "6a118f85e8feffff6a018f4424106a008f4424086a078f04244c89eeb9c003000041b81c0200006a314159e8d3d209009090909090"),
        (0x91f6e8, "488d0521b3ef00448b7824448b6028", "41bf8007000041bc38040000909090"),
        (0x910580, "470890040000c7470c91020000c747", "be80070000ba38040000e9610e0800"),
        (0x8e441c, "e8cfcf0a00", "e85fc10200"),
        (0x91a37a, "488d058f06f0008b4824c4e1fa2ad18b4028c4e1fa2ad8", "b980070000c4e1fa2ad1b838040000c4e1fa2ad8909090"),
        (0x9d4023, "8b7228c4e1fa2ac6c5fa100da96c7600c5f828d1c5ea5ec08b5224c4e1fa2acac5ea5ec9", "bed6b9723ac5f96ec6ba8988083ac5f96ecac5fa10159f6c760090909090909090909090"),
    ],
    "360": [
        (0x910740, "c5f828d889770889570cc747140000000089771889571c4189f0c4c1fa2ac089d2c4e1fa2acac5f259d3c5fa115f20c5ea5ed089d6c1ee024489c0c1e80344894724895728c5fa115f2cc5fa115710894740897744c7476800007042c7476c000000004489473889573c894f74c7475032000000c7475416000000c74758cd040000c7475cb9020000c747489c040000c7474ca4020000c74760ce040000c74764ba020000e946530e00660f1f440000", "be80020000ba68010000c5f828d889770889570cc747140000000089771889571c4189f0c4c17a2ac0c5fa2acac5f259d3c5fa115f20c5ea5ed089d6c1ee02b85000000044894724895728c5fa115f2cc5fa115710894740897744c7476800007042c7476c000000004489473889573c894f74c7475032000000c7475416000000c74758cd040000c7475cb9020000c747489c040000c7474ca4020000c74760ce040000c74764ba020000e940530e00"),
        (0x9104f2, "83fe030f848400000083fe020f85f6000000c7470800040000c7470c58020000c7471400000000c7471800040000c7471c58020000", "89d1be80020000ba68010000b80000803fc5f96ec0e934020000909090909090909090909090909090909090909090909090909090"),
        (0x9103bb, "80070000", "80020000"),
        (0x9103c2, "38040000", "68010000"),
        (0x9103d0, "80070000", "80020000"),
        (0x9103d7, "38040000", "68010000"),
        (0x9103e5, "80070000", "80020000"),
        (0x9103ec, "38040000", "68010000"),
        (0x910401, "f0000000", "50000000"),
        (0x910408, "0e010000", "5a000000"),
        (0x91042b, "80070000", "80020000"),
        (0x910432, "38040000", "68010000"),
        (0x910b22, "80070000", "80020000"),
        (0x910b29, "38040000", "68010000"),
        (0x910b3e, "80070000", "80020000"),
        (0x910b45, "38040000", "68010000"),
        (0x910b53, "80070000", "80020000"),
        (0x910b5a, "38040000", "68010000"),
        (0x910b6f, "f0000000", "50000000"),
        (0x910b76, "0e010000", "5a000000"),
        (0x910b99, "80070000", "80020000"),
        (0x910ba0, "38040000", "68010000"),
        (0x91adbc, "488d054dfcef008b5808895d8c448b600c44896588", "bb80020000895d8c41bc6801000044896588909090"),
        (0x92ef1a, "c78500ffffff11000000c744241001000000c744240800000000c70424070000004c89f64489e14189d841b93d000000e8c1d30900", "6a118f8500ffffff6a018f4424106a008f4424086a078f04244c89f6b98007000041b8380400006a3d4159e8c6d309009090909090"),
        (0x92ef7d, "c785f8feffff11000000c744241001000000c744240800000000c7042407000000488d95f8feffff4c89ee4489e14589f041b931000000e857d30900", "6a118f85f8feffff6a018f4424106a008f4424086a078f0424488d95f8feffff4c89eeb9c003000041b81c0200006a314159e85cd309009090909090"),
        (0x92efc0, "c785f0feffff11000000c744241001000000c744240800000000c7042407000000488d95f0feffff4c89ee89d94c8b85d0fcffff41b93d000000e811d30900", "6a118f85f0feffff6a018f4424106a008f4424086a078f0424488d95f0feffff4c89eeb98007000041b8380400006a3d4159e819d309009090909090909090"),
        (0x92f00d, "c785e8feffff11000000c744241001000000c744240800000000c70424070000004c89ee4489e14589f041b931000000e8ced20900", "6a118f85e8feffff6a018f4424106a008f4424086a078f04244c89eeb9c003000041b81c0200006a314159e8d3d209009090909090"),
        (0x91f6e8, "488d0521b3ef00448b7824448b6028", "41bf8007000041bc38040000909090"),
        (0x910580, "470890040000c7470c91020000c747", "be80070000ba38040000e9610e0800"),
        (0x8e441c, "e8cfcf0a00", "e85fc10200"),
        (0x91a37a, "488d058f06f0008b4824c4e1fa2ad18b4028c4e1fa2ad8", "b980070000c4e1fa2ad1b838040000c4e1fa2ad8909090"),
        (0x9d4023, "8b7228c4e1fa2ac6c5fa100da96c7600c5f828d1c5ea5ec08b5224c4e1fa2acac5ea5ec9", "bed6b9723ac5f96ec6ba8988083ac5f96ecac5fa10159f6c760090909090909090909090"),
    ],
}


# CUSA00552 01.00: give the in-game scene, depth, HUD, reticle and
# full/half-size postprocessing a SINGLE resolution domain.
# The 2026-11-06 Android log proved 720p depth alongside 1080p color.
# Disassembly of the user's original ELF at 0x92EF1A..0x92F00D confirms
# these are paired full/half framebuffer creations (0x9CC310 calls):
# the old performance profile incorrectly pinned them to 1920x1080
# and 960x540, independent of the selected 720/540/360 resolution.
#
# This profile does NOT modify video content, movie decoder, codecs or source
# frame dimensions. The video path may still present a native-size frame.
LOW_RES_SCENE = {"720": (1280, 720), "540": (960, 540), "360": (640, 360)}
# 01.11 verified from the user's original 24,220,371-byte CUSA00552 SELF.
# The previous 01.00 32-site table is NOT reused here. The newer executable
# stores dimensions in screeninfo.cfg defaults and a preset selector at
# 0xAB0600, while its framebuffer creation sites moved to 0xAD47xx.
# Every original span below has been checked against this exact SELF.
# Other 01.11 variants deliberately retain Original until independently checked.
# The owner's second 01.11 ELF differs by only 28 bytes in the
# 0x6C74E9 -> 0xC2C860 pointer-guard trampoline, outside ALL 24 validated
# 01.11 stability, effects, and unified-resolution patch spans.
# Both SELF variants were independently checked (24/24 original byte guards).
RESOLUTION_111_SELF_SHA256 = frozenset({
    "3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e",
    "31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7",
})
RESOLUTION_111_GUARDS = {
    0xAB05D6: "c7470880070000c7470c38040000",  # screen-info instance
    0xAB095D: "c7400880070000c7400c38040000",  # global screen-info
    0x116BF10: "8007000038040000f00000000e010000",  # screen-info defaults
    0x116BF30: "8007000038040000f00000000e010000",  # persistent defaults
    0xAB0600: "ffce83fe0777194863ce488d058fb9",  # screen-info mode selector
    0xAD478C: "b98007000041b838040000",  # full-size framebuffer
    0xAD47E7: "b9c003000041b81c020000",  # half-size framebuffer
    0x115FB9C: "d6b9723a00008744",  # HUD coordinate height inverse / height
}


def scene_aim_resolution_patches_111(resolution):
    """Version-guarded 01.11 scene/HUD/post targets; keep video sources native."""
    width, height = LOW_RES_SCENE[resolution]
    u32 = lambda n: struct.pack("<I", n)
    # Skip the old preset lookup entirely (its smallest preset is 1920x1080).
    # mov eax,width; mov ecx,height; jmp 0xAB062A (original field stores and
    # ratio/quarter-resolution calculations remain untouched).
    selector = (b"\xb8" + u32(width) + b"\xb9" + u32(height) +
                b"\xe9" + struct.pack("<i", 0xAB062A - (0xAB0600 + 15)))
    replacements = {
        0xAB05D6: bytes.fromhex("c74708") + u32(width) + bytes.fromhex("c7470c") + u32(height),
        0xAB095D: bytes.fromhex("c74008") + u32(width) + bytes.fromhex("c7400c") + u32(height),
        0x116BF10: struct.pack("<4I", width, height, width // 8, height // 4),
        0x116BF30: struct.pack("<4I", width, height, width // 8, height // 4),
        0xAB0600: selector,
        0xAD478C: b"\xb9" + u32(width) + b"\x41\xb8" + u32(height),
        0xAD47E7: b"\xb9" + u32(width // 2) + b"\x41\xb8" + u32(height // 2),
        0x115FB9C: struct.pack("<ff", 1.0 / height, float(height)),
    }
    if replacements.keys() != RESOLUTION_111_GUARDS.keys():
        raise ValueError("Perfil 01.11 de resolução incompleto")
    out = []
    for address, original in RESOLUTION_111_GUARDS.items():
        before, after = bytes.fromhex(original), replacements[address]
        if len(before) != len(after):
            raise ValueError(f"01.11: tamanho inválido em {address:#x}")
        out.append((address, original, after.hex()))
    return out


AIM_NORMALIZATION_ADDRESS = 0x9D4023
GAME_UI_NATIVE_OVERRIDES = {
    0x91F6E8: "41bf8007000041bc38040000909090",
    0x910580: "be80070000ba38040000e9610e0800",
    0x91A37A: "b980070000c4e1fa2ad1b838040000c4e1fa2ad8909090",
}
POSTPROCESS_FULL_SITES = (0x92EF1A, 0x92EFC0)
POSTPROCESS_HALF_SITES = (0x92EF7D, 0x92F00D)


def scene_aim_resolution_patches(resolution):
    """Guarded CUSA00552 1.00 scene/UI/post unified resolution profile."""
    width, height = LOW_RES_SCENE[resolution]
    scene_width, scene_height = struct.pack("<I", width).hex(), struct.pack("<I", height).hex()
    half_width = struct.pack("<I", width // 2).hex()
    half_height = struct.pack("<I", height // 2).hex()
    patches = []
    visited = set()
    for at, original, raw in PERFORMANCE_RESOLUTIONS[resolution]:
        replacement = raw
        if at in GAME_UI_NATIVE_OVERRIDES:
            if raw != GAME_UI_NATIVE_OVERRIDES[at]:
                raise ValueError("Patch UI/resolução fora do perfil CUSA00552 verificado")
            if raw.count("80070000") != 1 or raw.count("38040000") != 1:
                raise ValueError("Constantes de viewport da UI não reconhecidas")
            replacement = raw.replace("80070000", scene_width).replace("38040000", scene_height)
            visited.add(at)
        elif at in POSTPROCESS_FULL_SITES or at in POSTPROCESS_HALF_SITES:
            # Replace just the mov ecx,width / mov r8d,height argument pair
            # to the frame allocator. Preserve call displacement and all
            # alignment/flags/target format arguments byte for byte.
            full = at in POSTPROCESS_FULL_SITES
            expected = "b98007000041b838040000" if full else "b9c003000041b81c020000"
            scaled = ("b9" + (scene_width if full else half_width) +
                      "41b8" + (scene_height if full else half_height))
            if raw.count(expected) != 1:
                raise ValueError("Alocação de framebuffer full/half fora do perfil verificado")
            replacement = raw.replace(expected, scaled)
            visited.add(at)
        elif at == AIM_NORMALIZATION_ADDRESS:
            insn = bytearray.fromhex(raw)
            if (len(insn) < 18 or insn[0] != 0xBE or
                    insn[5:10] != bytes.fromhex("c5f96ec6ba") or
                    insn[14:18] != bytes.fromhex("c5f96eca")):
                raise ValueError("Instruções de normalização da mira desconhecidas")
            y, x = struct.unpack_from("<f", insn, 1)[0], struct.unpack_from("<f", insn, 10)[0]
            if abs(y - 1.0 / 1080.0) > 1e-9 or abs(x - 1.0 / 1920.0) > 1e-9:
                raise ValueError("Constantes anteriores da mira são diferentes do esperado")
            struct.pack_into("<f", insn, 1, 1.0 / height)
            struct.pack_into("<f", insn, 10, 1.0 / width)
            replacement = insn.hex()
            visited.add(at)
        if len(replacement) != len(raw):
            raise ValueError("Tamanho do patch de resolução modificado")
        patches.append((at, original, replacement))
    required = {*GAME_UI_NATIVE_OVERRIDES, *POSTPROCESS_FULL_SITES,
                *POSTPROCESS_HALF_SITES, AIM_NORMALIZATION_ADDRESS}
    if visited != required:
        raise ValueError("Perfil de resolução unificada incompleto")
    return patches


def selected_options(options, profile="01.00", self_sha=None):
    options = options or {}
    if not isinstance(options, dict):
        raise ValueError("Configuração de patches inválida")
    resolution = options.get("resolution", "original")
    if not isinstance(resolution, str) or resolution not in {"original", "720", "540", "360"}:
        raise ValueError("Resolução não suportada")
    selected = {}
    if resolution != "original":
        if profile == "01.00":
            selected["resolution-" + resolution] = scene_aim_resolution_patches(resolution)
        elif profile == "01.11" and self_sha in RESOLUTION_111_SELF_SHA256:
            selected["resolution-" + resolution] = scene_aim_resolution_patches_111(resolution)
        else:
            raise ValueError("Patch de resolução 01.11 ainda não validado para este SHA-256")
    effects = PERFORMANCE_EFFECTS if profile == "01.00" else PERFORMANCE_EFFECTS_111
    for name, changes in effects.items():
        if type(options.get(name, False)) is not bool:
            raise ValueError("Opção de patch inválida: " + name)
        if options.get(name, False):
            selected["disable-" + name] = changes
    return selected


def base_patches_for(options, profile="01.00"):
    patches = dict(BASE_PATCHES if profile == "01.00" else BASE_PATCHES_111)
    # The 01.00 job-sleep patch forces three formerly conditional wake paths.
    # Snapdragon 7+ Gen 2 had a persistent job-counter deadlock while the
    # presentation thread kept flipping; retain the original game branches
    # on this device instead of globally disabling the job watchdog.
    job_sleep = (options or {}).get("job_sleep", True)
    if type(job_sleep) is not bool:
        raise ValueError("Opção de workers inválida")
    if not job_sleep:
        patches.pop("job-sleep")
    return patches


def write_selected(source, prepared, options):
    """Atomically publish launch patches; never change the player's SELF or linked image."""
    import importlib.util
    import json
    from pathlib import Path
    prepared, source = Path(prepared), Path(source)
    game_self = prepared / "tlou-view/eboot.bin"
    if not game_self.is_file():
        raise ValueError("Esta pasta preparada não contém eboot original")
    digest = hashlib.sha256(game_self.read_bytes()).hexdigest()
    profile = known_profile(digest)
    if profile is None:
        if (options or {}).get("resolution", "original") != "original" or any(
            (options or {}).get(name, False) for name in PERFORMANCE_EFFECTS
        ):
            raise ValueError("Eboot não reconhecido: patches exigem SHA-256 validado")
        # A prepared directory may be manually reused after replacing the game:
        # never leave a stale patches.bin from a different executable active.
        (prepared / "patches.bin").unlink(missing_ok=True)
        (prepared / "patch-validation.json").unlink(missing_ok=True)
        print("Eboot sem perfil Android de patches conhecido; removidos patches antigos.", flush=True)
        return
    effective = dict(options or {})
    # Never apply 01.00 offsets to an unknown 01.11 binary variant.
    # The supplied 3cfa... SELF has a separately validated guarded layout.
    if (profile == "01.11" and digest not in RESOLUTION_111_SELF_SHA256 and
            effective.get("resolution", "original") != "original"):
        print("Aviso: resolução reduzida sem assinatura validada nesta variante do eboot 01.11; usando Original.", flush=True)
        effective["resolution"] = "original"
    requested = selected_options(effective, profile, digest)
    spec = importlib.util.spec_from_file_location("tlou_patch_writer", source / "tools/probe_tlou.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    elf_path = prepared / "eboot.elf"
    if not elf_path.is_file():
        # Imported prepared folders may contain only the linked image. Rebuild
        # the inspection ELF from the validated SELF without relinking the game.
        spec = importlib.util.spec_from_file_location("tlou_owned_self", source / "gow3_pc/scripts/prepare.py")
        inspector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(inspector)
        raw, _, _, _, _ = inspector.parse_self(game_self.read_bytes())
        inspection_temp = prepared / "eboot.elf.new"
        inspection_temp.write_bytes(raw)
        inspection_temp.replace(elf_path)
    elf = elf_path.read_bytes()
    phoff = struct.unpack_from("<Q", elf, 0x20)[0]
    size, count = struct.unpack_from("<HH", elf, 0x36)
    segments = [struct.unpack_from("<IIQQQQQQ", elf, phoff + i*size) for i in range(count)]
    def read(at, length):
        for kind, _, offset, start, _, filesz, _, _ in segments:
            if kind == 1 and start <= at and at + length <= start + filesz:
                return elf[offset + at-start:offset + at-start+length]
        return b""
    patches = base_patches_for(effective, profile)
    patches.update(requested)
    for name, changes in patches.items():
        for at, expected, _ in changes:
            if read(at, len(bytes.fromhex(expected))) != bytes.fromhex(expected):
                raise ValueError("Assinatura divergente no patch " + name + " em " + hex(at))
    temp = prepared / "patches.bin.new"
    module.image_patches(elf_path, temp, patches)
    temp.replace(prepared / "patches.bin")
    report = {"profile": digest, "game_version": profile, "selected": list(requested),
              "resolution": effective.get("resolution", "original"),
              "requested_resolution": (options or {}).get("resolution", "original"),
              "validated": True}
    (prepared / "patch-validation.json").write_text(json.dumps(report, indent=2)+"\n")
    print("Patches validados: " + (", ".join(requested) or "Original; correções de estabilidade"), flush=True)
