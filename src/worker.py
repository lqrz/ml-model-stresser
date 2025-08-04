# worker.py
import socket
# import json
import sys
import os

def process_request(message):
    return f"Processed message: '{message}' by Python PID {os.getpid()}"

def main(port):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_socket.bind(('localhost', port))
    print(f"Python worker listening on port {port}")

    while True:
        message, address = server_socket.recvfrom(1024)
        server_socket.sendto('Response from worker'.encode(), address)
            #sys.stdout.write(data.decode())
            #sys.stdout.flush()
            #request = json.loads(data.decode())
            #message = request.get("message", "")
            #response = {"response": process_request(message)}
            #client_socket.sendall(json.dumps(response).encode())

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python worker.py <port>")
        sys.exit(1)
    port = int(sys.argv[1])
    main(port)

