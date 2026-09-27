"""Read-only TLS 1.2 probe using exactly the root CA compiled into XTouch."""
from pathlib import Path
import hashlib
import re
import socket
import ssl

source = (Path(__file__).resolve().parent.parent / "src/xtouch/bbl-certs.h").read_text()
pem = re.search(r'const char \*us_mqtt_bambulab_com = R"PEM\((.*?)\)PEM";', source, re.S).group(1)
print("Bundled root SHA-256:", hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest())
for host in ("us.mqtt.bambulab.com", "cn.mqtt.bambulab.com"):
    # Do not load system roots: this tests the firmware's CA specifically.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_verify_locations(cadata=pem)
    with socket.create_connection((host, 8883), timeout=10) as raw:
        with context.wrap_socket(raw, server_hostname=host) as secure:
            print("PASS", host, secure.version(), "hostname + chain verified; no MQTT login sent")
