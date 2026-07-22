#!/bin/bash
set -e

SSL_DIR="/etc/nginx/ssl"
CERT_FILE="${SSL_DIR}/ccme.crt"
KEY_FILE="${SSL_DIR}/ccme.key"

if [ -f "$CERT_FILE" ] && [ -f "$KEY_FILE" ]; then
    echo "Certificates already exist. Overwrite? [y/N]"
    read -r answer
    if [ "$answer" != "y" ] && [ "$answer" != "Y" ]; then
        echo "Aborted."
        exit 0
    fi
fi

mkdir -p "$SSL_DIR"

echo "Generating self-signed certificate for ccme.local..."
openssl req -x509 -nodes -days 3650 \
    -newkey rsa:2048 \
    -keyout "$KEY_FILE" \
    -out "$CERT_FILE" \
    -subj "/C=CN/ST=Beijing/L=Beijing/O=CCME/CN=ccme.local" \
    -addext "subjectAltName=DNS:ccme.local,DNS:localhost,IP:127.0.0.1"

chmod 600 "$KEY_FILE"
chmod 644 "$CERT_FILE"

echo "Done. Certificate: ${CERT_FILE}"
echo "       Key:        ${KEY_FILE}"
