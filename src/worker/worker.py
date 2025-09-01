"""
worker.py
---------

A simple UDP Python worker process that receives messages from the C UDP server,
processes them, and returns responses.

Workflow:
    1. The worker is started with a port number argument.
    2. It binds a UDP socket to localhost:<port>.
    3. It waits for incoming messages from the C server.
    4. On each message, it sends a response back to the sender.

This worker is designed to integrate with the UDP C server + thread pool.
"""

import socket
import sys
import os

def process_request(message):
    """
    Process an incoming request message.

    Args:
        message (str): The raw message received from the server.

    Returns:
        str: A processed response string including the worker's PID.
    """
    return f"Processed message: '{message}' by Python PID {os.getpid()}"

def main(port):
    """
    Start the worker UDP server loop.

    Binds a UDP socket to the given port and continuously handles requests.

    Args:
        port (int): The port number where the worker listens.
    """
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

