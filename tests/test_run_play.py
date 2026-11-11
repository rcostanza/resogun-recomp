from paths import ROOT
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(ROOT))
import run


class RunPlayTests(unittest.TestCase):
    def test_shared_cache_reaches_every_exe_run(self):
        # The GPU library's folder is the shared cache from the first run of the exe (--cpu-features) on,
        # never the save folder, so test boots on a copy of the save do not write to user/.
        seen = []
        features = mock.Mock(returncode=0, stdout='CPU: complete\n')
        process = mock.Mock()
        process.wait.return_value = 0

        def exe_run(command, **kwargs):
            seen.append((os.environ.get('RESOGUN_GPU_USER_DIR'), os.environ.get('RESOGUN_SHADER_DIR')))
            return features

        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder) / 'game'
            game.mkdir()
            (game / 'eboot.bin').write_bytes(b'')
            user = Path(folder) / 'user-copy'
            cache = Path(folder) / 'cache'
            argv = ['run.py', '--game', str(game), '--user', str(user), '--timeout', '20']
            with mock.patch.dict(os.environ, {'RESOGUN_ISA_REWRITE': '0', 'RESOGUN_GPU_USER_DIR': str(cache)}), \
                    mock.patch.object(sys, 'argv', argv), \
                    mock.patch.object(run, 'prepare'), \
                    mock.patch.object(run, 'find_exe', return_value=Path('resogun.exe')), \
                    mock.patch.object(run.subprocess, 'run', side_effect=exe_run), \
                    mock.patch.object(run.subprocess, 'Popen', return_value=process) as popen:
                os.environ.pop('RESOGUN_SHADER_DIR', None)
                self.assertEqual(run.main(), 0)
                # Shader caches go to shaders/ next to the exe, as an installed copy's.
                self.assertEqual(seen, [(str(cache), str(Path('shaders')))])
                self.assertIn(str(user.resolve()), popen.call_args.args[0])
            self.assertTrue(user.is_dir())

    def test_shared_cache_dir_is_per_pc(self):
        env = {'LOCALAPPDATA': r'C:\Users\a\AppData\Local', 'XDG_CACHE_HOME': '/home/a/.cache'}
        expected = Path(env['LOCALAPPDATA']) / 'Resogun' if run.WINDOWS else Path('/home/a/.cache/resogun')
        self.assertEqual(run.shared_cache_dir(env), expected)
        self.assertEqual(run.shared_cache_dir({**env, 'RESOGUN_GPU_USER_DIR': 'elsewhere'}), Path('elsewhere'))


if __name__ == '__main__':
    unittest.main()
