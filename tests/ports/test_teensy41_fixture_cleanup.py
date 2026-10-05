import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/ports'))
from teensy41_fixture_cleanup import cleanup_fixtures

class CleanupTests(unittest.TestCase):
    root='/flash/_solaros_flash_0123456789'
    def test_valid_roots_and_confirmed_removal(self):
        calls=[];report={}
        roots=[self.root,'/sd/_apps_01234567','/usb/_usb_test_abcdef01','/_solaros_pynet_0123456789']
        def command(s):
            calls.append(s)
            return 'user@teensy41:/ ' if s.startswith('rm ') else 'ls: cannot open '+s[3:]+': No such file or directory\n'
        cleanup_fixtures(command,roots,report)
        self.assertEqual(report['fixtures_removed'],roots)
        self.assertEqual(report['fixtures_retained'],[])
        self.assertEqual(len(calls),8)
    def test_refuses_entire_unsafe_batch_before_deleting(self):
        for path in ['/flash','/flash/lib','/flash/.ssh',self.root+'/../lib',self.root+'/file',self.root+'; reboot','/sd/_apps_BAD','/flash/_apps_01234567\n']:
            with self.subTest(path=path),self.assertRaises(ValueError):
                cleanup_fixtures(lambda s:self.fail('command issued'),[self.root,path],{})
    def test_keep_never_sends_commands(self):
        report={};cleanup_fixtures(lambda s:self.fail('command issued'),[self.root],report,True)
        self.assertEqual(report['fixtures_retained'],[self.root])
    def test_delete_error_and_failed_confirmation_stop_batch(self):
        for responses in [['rm: busy'],['','listing still present']]:
            calls=[];report={}
            def command(s):calls.append(s);return responses[len(calls)-1]
            with self.assertRaises(RuntimeError):cleanup_fixtures(command,[self.root,'/sd/_apps_01234567'],report)
            self.assertEqual(len(calls),len(responses))
            self.assertEqual(report['fixtures_removed'],[])
            self.assertEqual(len(report['fixtures_retained']),2)
    def test_duplicate_roots_rejected(self):
        with self.assertRaises(ValueError):cleanup_fixtures(lambda s:self.fail('command issued'),[self.root,self.root],{})

if __name__=='__main__':unittest.main()
