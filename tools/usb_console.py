#!/usr/bin/env python3
"""
Testovaci konzole pro CykloComp pres USB (misto BLE appky).

Pouziva stejny textovy radkovy protokol jako BLE prikazovy kanal
(viz firmware/CykloComp/CykloComp.ino - handleCommand()). ESP posila
telemetrii JSON 1x/s na Serial - ten se tu prubezne vypisuje.

Instalace zavislosti:
    pip install pyserial

Pouziti:
    python3 tools/usb_console.py [PORT] [BAUD]

    PORT vychozi: /dev/ttyACM0 (Linux/Mac) - na Windows napr. COM5
    BAUD vychozi: 115200

V interaktivni konzoli pak muzes psat primo protokolove prikazy, napr.:
    RIDE:START
    SCREEN:2
    CFG:FIELDS:0,1,3,4
    SET_TZ:2
    RESET_TRIP

Pro odeslani testovaci trasy napis:
    testroute
a skript sam odesle RT:BEGIN / RT:P:... / RT:END s nekolika ukazkovymi body.
"""
import sys
import threading
import time

try:
    import serial
except ImportError:
    print("Chybi pyserial. Nainstaluj: pip install pyserial")
    sys.exit(1)

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200

# par bodu okolo Prahy jen pro rychly test vykresleni trasy na displeji
TEST_ROUTE = [
    (50.0755, 14.4378),
    (50.0800, 14.4420),
    (50.0850, 14.4500),
    (50.0900, 14.4600),
    (50.0950, 14.4700),
]


def reader_thread(ser):
    while True:
        try:
            line = ser.readline()
        except serial.SerialException:
            return
        if not line:
            continue
        try:
            text = line.decode("utf-8", errors="replace").rstrip()
        except Exception:
            continue
        if text:
            print(f"< {text}")


def send_line(ser, line):
    ser.write((line + "\n").encode("utf-8"))
    print(f"> {line}")


def send_route(ser, points, chunk_size=6):
    send_line(ser, "RT:CLEAR")
    send_line(ser, "RT:BEGIN")
    for i in range(0, len(points), chunk_size):
        chunk = points[i:i + chunk_size]
        payload = ";".join(f"{lat},{lng}" for lat, lng in chunk) + ";"
        send_line(ser, "RT:P:" + payload)
        time.sleep(0.05)
    send_line(ser, "RT:END")


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BAUD

    print(f"Pripojuji se na {port} @ {baud}...")
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(1.5)  # ESP32-C3 po otevreni USB-CDC chvili resetuje

    t = threading.Thread(target=reader_thread, args=(ser,), daemon=True)
    t.start()

    print("Pripojeno. Piš prikazy (napr. RIDE:START), 'testroute' pro ukazkovou trasu, 'quit' pro konec.")
    try:
        while True:
            line = input()
            if not line:
                continue
            if line.strip().lower() == "quit":
                break
            if line.strip().lower() == "testroute":
                send_route(ser, TEST_ROUTE)
                continue
            send_line(ser, line)
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
