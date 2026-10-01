#!/usr/bin/env python3

import errno
import json
import os
import socket
import subprocess
import time

from email.mime.text import MIMEText

import pytest


def pytest_collect_file(parent, file_path):
    if os.access(str(file_path), os.X_OK) and file_path.name.startswith('cmocka_'):
        return CMockaFile.from_parent(parent, path=file_path)


class CMockaFile(pytest.File):
    def collect(self):
        out = subprocess.run(
            str(self.fspath),
            env={
                'CMOCKA_MESSAGE_OUTPUT': 'TAP',
            },
            capture_output=True,
            text=True,
        )
        lines = out.stdout.splitlines()
        if lines[0].startswith('TAP version'):
            lines.pop(0)
        plan = lines[0].split('..')
        if len(plan) != 2:
            yield CMockaItem.from_parent(self, line='not ok - cmocka', output=out.stdout)
            plan = ('', '0')

        count = 0
        for line in lines[1:]:
            if not line.startswith('ok') and not line.startswith('not ok'):
                continue
            count += 1
            yield CMockaItem.from_parent(self, line=line, output=out.stdout)

        if count != int(plan[1]):
            yield CMockaItem.from_parent(self, line='not ok - cmocka_tap_plan', output=out.stdout)


class CMockaItem(pytest.Item):
    def __init__(self, *, line, output=None, **kwargs):
        name = line.split(' - ')[1]
        super(CMockaItem, self).__init__(name, **kwargs)
        self.line = line
        self.output = output

    def runtest(self):
        if self.line.startswith('not ok'):
            raise CMockaException(self)

    def repr_failure(self, excinfo):
        if isinstance(excinfo.value, CMockaException):
            return self.output


class CMockaException(Exception):
    """ custom exception """


def openport(port):
    # Find a usable port by iterating until there's an unconnectable port
    while True:
        try:
            socket.create_connection(('localhost', port), 0.1)
            port += 1
            if port > 65535:
                raise ValueError("exhausted TCP port range without finding a free one")
        except socket.error:
            return port


def redis():
    port = openport(6379)

    with open(os.devnull, 'w') as devnull:
        redis_proc = None
        try:
            redis_proc = subprocess.Popen(['redis-server', '--port', str(port)], stdout=devnull, stderr=devnull)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise
            return None

        running = False
        i = 0
        while not running:
            i += 1
            try:
                socket.create_connection(('localhost', port), 0.1)
                running = True
            except socket.error:
                if i > 20:
                    raise
                time.sleep(0.1)

        return {
            'port': port,
            'proc': redis_proc,
        }


@pytest.fixture(scope='session')
def tool_path():
    def _tool_path(tool):
        binpath = os.path.dirname(os.path.realpath(__file__))
        binpath = os.path.join(binpath, '..', tool)
        return os.path.realpath(binpath)
    return _tool_path


@pytest.fixture(
    params=[
        'lmdb',
        'null',
        'redis',
    ],
)
def run_simvacation(request, tmp_path_factory, tool_path):
    tmpdir = str(tmp_path_factory.mktemp('simvacation'))
    cfile = os.path.join(tmpdir, 'simvacation.conf')

    config = {
        'core': {
            'vdb': request.param,
            'vlu': 'null',
            'interval': 2,
            'sendmail': tool_path('test/sendmail') + ' -f "" $R',
            'domain': 'example.com',
        },
        'redis': {
            'host': '127.0.0.1',
        },
        'lmdb': {
            'path': os.path.join(tmpdir, 'lmdb'),
        }
    }

    redconf = None
    if request.param == 'redis':
        redconf = redis()
        if not redconf:
            pytest.skip('redis-server not found')
        config['redis']['port'] = redconf['port']

    elif request.param == 'lmdb':
        os.mkdir(config['lmdb']['path'])

    elif 'suppress' in request.function.__name__:
        pytest.xfail('The null VDB does not support storing state')

    if 'ldap' in request.function.__name__:
        server = os.environ.get('LDAP_SERVER')
        if not server:
            pytest.skip('Environment variable LDAP_SERVER is not set')
        config['core']['vlu'] = 'ldap'
        config['ldap'] = {
            'uri': server,
            'search_base': 'ou=People,dc=example,dc=com',
            'group_search_base': 'ou=Groups,dc=example,dc=com',
        }

    with open(cfile, 'w') as f:
        f.write(json.dumps(config, indent=4))

    def _run_simvacation(sender, rcpt, msg, outdir):
        return subprocess.run(
            [
                tool_path('simvacation'),
                '-c', cfile,
                '-f', sender,
                rcpt,
            ],
            env={
                'PYTEST_TMPDIR': outdir,
            },
            input=msg,
            check=True,
            capture_output=True,
            text=True,
        )

    yield _run_simvacation

    if redconf:
        redconf['proc'].terminate()


@pytest.fixture
def testmsg(request):
    msg = MIMEText(request.function.__name__)
    msg['Subject'] = 'simta test message for {}'.format(request.function.__name__)
    msg['From'] = 'testsender@example.com'
    msg['To'] = 'testrcpt@example.com'
    return msg
