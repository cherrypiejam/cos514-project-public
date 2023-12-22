import sys, os, time, socket
import pexpect

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def main():
    if len(sys.argv) < 3:
        eprint("USAGE: [qemu command line] [monitor socket]")

    p = pexpect.spawn(sys.argv[1], encoding="utf-8", timeout=180)
    p.logfile = sys.stdout # pipe all output to stdout

    # Wait until QEMU monitor socket appears and connect
    mc = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(60 * 10):
        if os.path.exists(sys.argv[2]):
            break
        time.sleep(0.1)
    mc.connect(sys.argv[2])

    p.expect("OpenSBI v")
    eprint("===> OpenSBI boot stage")

    p.expect("NixOS Stage 1")
    eprint("===> Reached NixOS Stage 1")

    p.expect("NixOS Stage 2")
    eprint("===> Reached NixOS Stage 2")

    p.expect("starting systemd...")
    eprint("===> Reached systemd start")

    p.expect("Welcome to NixOS")
    eprint("===> Completed systemd boot, reached NixOS welcome message")

    p.expect("root@encapfn-dev")
    eprint("===> Completed boot, reached shell")

    eprint("Requesting savevm and quit")
    mc.send(b'savevm booted\nquit\n')

    eprint("Waiting for EOF...")
    p.expect(pexpect.EOF)

    eprint("Done!")

if __name__ == "__main__":
    main()
