"""Read-only hook catalog contract; not a gameplay capture."""
import unittest
import patch_variants as variants

class RenderAudit(unittest.TestCase):
    def test_exact_guards(self):
        sites = dict((a,bytes.fromhex(s)) for a,s in variants.RENDER_AUDIT_SITES)
        read=lambda address,size:sites.get(address,b"")[:size]
        result=variants.render_audit_catalog(read,[(0,0x1473db8,True)])
        self.assertEqual(len(result.splitlines()),10)
        self.assertTrue(all(line.startswith("hook ") and line.endswith(" regs") for line in result.splitlines()))
        with self.assertRaisesRegex(ValueError,"guard mismatch"):
            variants.render_audit_catalog(read,[(0,0x1473db8,False)])
        with self.assertRaisesRegex(ValueError,"guard mismatch"):
            variants.render_audit_catalog(lambda a,n:bytes(n),[(0,0x1473db8,True)])
