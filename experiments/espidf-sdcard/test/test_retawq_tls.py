"""Test the real browser and mbedTLS against local HTTPS fixtures."""

from contextlib import contextmanager
import os
from pathlib import Path
import shlex
import shutil
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest


ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "experiments/espidf-sdcard/components/retawq"

DRIVER = r'''
#include "resource.c"
#include "retawq.h"
#include <assert.h>

static mbedtls_x509_crt roots;
extern void test_config_warning(void);
int esp_crt_bundle_attach(void *config)
{ mbedtls_ssl_conf_ca_chain(config, &roots, NULL); return 0; }

static tConnCbEventKind observed;
static void record_event(tConnection *conn, tConnCbEventKind event)
{ (void)conn; observed = event; }

int main(int argc, char **argv)
{
  assert(argc == 3);
  if (strcmp(argv[1], "warning") == 0) {
    test_config_warning();
    return 0;
  }
  if (strcmp(argv[1], "selftest") == 0) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    tConnection conn = {0};
    conn.fd = sockets[0]; conn.callback = record_event;
    fd_observe_init();
    assert(tls_mbedtls_wait(&conn, MBEDTLS_ERR_SSL_WANT_READ));
    assert(conn.flags & cnfReading);
    assert(tls_mbedtls_wait(&conn, MBEDTLS_ERR_SSL_WANT_WRITE));
    assert(conn.flags & cnfWriting);
    assert(!tls_mbedtls_wait(&conn, MBEDTLS_ERR_X509_CERT_VERIFY_FAILED));
    conn.flags = cnfConnected | cnfTlsDedication | cnfWantToWrite;
    conn_io_handler(&conn, fdofRead);
    assert(observed == ccekWrite);
    conn.flags &= ~cnfWantToWrite;
    conn_io_handler(&conn, fdofWrite);
    assert(observed == ccekRead);
    close(sockets[0]); close(sockets[1]);
    return 0;
  }
  assert(psa_crypto_init() == PSA_SUCCESS);
  mbedtls_x509_crt_init(&roots);
  assert(mbedtls_x509_crt_parse_file(&roots, argv[1]) == 0);
  const char *args[] = {"retawq", argv[2]};
  int result = retawq_main(2, args);
  mbedtls_x509_crt_free(&roots);
  return result;
}
'''


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stdout[-2000:] + result.stderr[-4000:])
    return result


class HttpsHandler(socketserver.StreamRequestHandler):
    def handle(self):
        while self.rfile.readline().strip():
            pass
        # One TLS record contains both headers and several buffered body reads.
        body = b"<html><body>" + b"buffered TLS data " * 300 + b"RETAWQ_TLS_OK</body></html>"
        headers = ("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                   f"Content-Length: {len(body)}\r\n\r\n").encode()
        self.request.sendall(headers + body)
        self.server.release.wait(15)


class TlsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        work = ROOT / "build/retawq-tls-test"
        work.mkdir(parents=True, exist_ok=True)
        sdk = ROOT / "external/esp-idf/components/mbedtls/mbedtls"
        source = work / "mbedtls"
        # ESP-IDF leaves the top-level framework empty; keep host build changes out of the submodule.
        shutil.copytree(sdk, source, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns(".git"))
        shutil.copytree(sdk / "tf-psa-crypto/framework", source / "framework",
                        dirs_exist_ok=True)
        compat = work / "include/mbedtls"
        compat.mkdir(parents=True, exist_ok=True)
        for name in ("bignum", "ecp"):
            (compat / (name + ".h")).write_text(
                '#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS\n'
                f'#include "mbedtls/private/{name}.h"\n')
        (compat.parent / "esp_crt_bundle.h").write_text(
            '#include "mbedtls/ssl.h"\nint esp_crt_bundle_attach(void *config);\n')
        build = work / "build"
        run(["cmake", "-S", str(source), "-B", str(build),
             "-DENABLE_TESTING=OFF", "-DENABLE_PROGRAMS=OFF",
             "-DMBEDTLS_FATAL_WARNINGS=OFF",
             "-DPython3_EXECUTABLE=" + sys.executable,
             "-DMBEDTLS_FRAMEWORK_DIR=" + str(source / "framework"),
             "-DCMAKE_C_FLAGS=-I" + str(compat.parent)], timeout=60)
        run(["cmake", "--build", str(build), "-j", "4"], timeout=120)
        driver = work / "driver.c"
        driver.write_text(DRIVER)
        init_driver = work / "init_warning.c"
        init_driver.write_text(
            '#include "init.c"\n'
            'void test_config_warning(void) {\n'
            '  fd_stderr = 2; __lfdmbs = 4; linecount = (size_t)-1;\n'
            '  warn_section(cscUnknown, "test warning");\n}\n')
        command = shlex.split(os.environ.get("CC", "cc"))
        for name in ("CFLAGS", "SANFLAGS"):
            command += shlex.split(os.environ.get(name, ""))
        command += ["-D_DEFAULT_SOURCE", "-w"]
        for include in (COMPONENT, COMPONENT / "include", compat.parent,
                        source / "include", source / "tf-psa-crypto/include",
                        source / "tf-psa-crypto/drivers/builtin/include",
                        build / "library", build / "tf-psa-crypto/include",
                        build / "tf-psa-crypto/core"):
            command += ["-I" + str(include)]
        command += [str(init_driver)]
        command += [str(COMPONENT / name) for name in
                    ("main.c", "parser.c", "stuff.c", "cursesbi.c")]
        cls.binary = work / "retawq"
        command += [str(driver), str(build / "library/libmbedtls.a"),
                    str(build / "library/libmbedx509.a"),
                    str(build / "library/libtfpsacrypto.a"),
                    "-o", str(cls.binary)]
        command += shlex.split(os.environ.get("LDFLAGS", ""))
        run(command, timeout=60)

        cls.fixtures = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.fixtures.cleanup)
        cls.certdir = Path(cls.fixtures.name)
        run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-subj", "/CN=Retawq test CA", "-days", "2",
             "-keyout", "ca.key", "-out", "ca.pem"], cwd=cls.certdir)
        run(["openssl", "req", "-newkey", "rsa:2048", "-nodes",
             "-subj", "/CN=localhost", "-keyout", "server.key",
             "-out", "server.csr"], cwd=cls.certdir)
        for name, hostname, days in (("valid", "localhost", "2"),
                                      ("wrong-host", "other.test", "2"),
                                      ("expired", "localhost", "expired")):
            (cls.certdir / "extensions").write_text(
                f"subjectAltName=DNS:{hostname}\nbasicConstraints=CA:FALSE\n")
            validity = (["-not_before", "20200101000000Z", "-not_after", "20210101000000Z"]
                        if days == "expired" else ["-days", days])
            run(["openssl", "x509", "-req", "-in", "server.csr",
                 "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
                 *validity, "-extfile", "extensions", "-out", name + ".pem"],
                cwd=cls.certdir)
        run(["openssl", "req", "-x509", "-key", "server.key", "-days", "2",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
             "-out", "untrusted.pem"], cwd=cls.certdir)

    @contextmanager
    def server(self, certificate):
        with socketserver.ThreadingTCPServer(("127.0.0.1", 0), HttpsHandler) as server:
            server.daemon_threads = True
            server.release = threading.Event()
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.maximum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(self.certdir / (certificate + ".pem"),
                                    self.certdir / "server.key")
            server.socket = context.wrap_socket(server.socket, server_side=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                yield f"https://localhost:{server.server_address[1]}/"
            finally:
                server.release.set()
                server.shutdown()
                thread.join(timeout=2)

    def invoke(self, *args):
        env = os.environ.copy()
        # shortcut: retawq's existing process-style teardown leaks; enable leak checks after its lifecycle cleanup.
        env["ASAN_OPTIONS"] = "detect_leaks=0"
        return subprocess.run([str(self.binary), *args], capture_output=True,
                              text=True, env=env, timeout=10)

    def test_nonblocking_retry_direction(self):
        result = self.invoke("selftest", "unused")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_warning_formats_full_size_t_line_number(self):
        result = self.invoke("warning", "unused")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"(line {sys.maxsize * 2 + 1}): test warning", result.stderr)

    def test_trusted_https_and_buffered_records(self):
        with self.server("valid") as url:
            result = self.invoke(str(self.certdir / "ca.pem"), "--dump=" + url)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("RETAWQ_TLS_OK", result.stdout)

    def test_reject_invalid_certificates(self):
        for certificate in ("wrong-host", "expired", "untrusted"):
            with self.subTest(certificate=certificate), self.server(certificate) as url:
                result = self.invoke(str(self.certdir / "ca.pem"), "--dump=" + url)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertNotIn("RETAWQ_TLS_OK", result.stdout)
                self.assertIn("TLS", result.stderr)
