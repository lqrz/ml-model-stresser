# ml-model-stresser

A small framework to **stress-test Python ML models** with concurrent inference locally.  
C handles **fast I/O** and process management, while Python workers handle the actual ML prediction.  
You can drive load against the C server and observe how **throughput** and **latency** behave as it juggles multiple Python model workers.

A lightweight **C server** accepts requests and fans them out to multiple **Python worker processes** so you can stress model serving under parallel load.

---

## Why?

`ml-model-stresser` solves a specific pain point in ML engineering:

> **"How do I realistically simulate concurrent load on my model without deploying to production or setting up a heavyweight serving infrastructure?"**

---

## Advantages

### 1. High-Performance Networking Layer (C)
- Low latency and high throughput request dispatch.
- Avoids Python’s GIL bottlenecks for networking — the C server can handle many client connections and multiplex them to Python workers efficiently.

### 2. Parallel Model Execution
- Uses **multiple separate Python processes** (not threads), bypassing GIL limitations.
- Each worker runs independently, similar to real production inference servers.

### 3. Flexible Protocol Choice
- **TCP mode:** reliable, connection-oriented.
- **UDP mode:** fast, lightweight, fire-and-forget.
- Lets you test both *API-style* and *streaming/datagram-style* loads.

### 4. Pluggable Model Code
- Drop in your own Python model-serving logic (PyTorch, TensorFlow, scikit-learn, etc.) without changing the C server code.
- Keeps stress-testing logic separate from ML logic.

### 5. Local, Lightweight, No Dependencies
- No Kubernetes, Docker, Ray, Triton, or heavy orchestration required.
- Minimal setup — compile the C code, run the workers, and start sending requests.

### 6. Deterministic & Reproducible Load
- Control number of workers, message size, request frequency, and protocol.
- Easy to benchmark before deploying new models.

---

## How It Works

### **Server Side (C)**
- Two implementations:
  - **TCP:** [`src/server/server_tcp.c`](src/server/server_tcp.c)
  - **UDP:** [`src/server/server_udp.c`](src/server/server_udp.c)
- Both listen on **port 6160**.
- On startup:
  - Forks multiple Python workers (**default:** 3).
  - Assigns worker ports starting at **9001** (round-robin dispatch for TCP).
  - Wraps messages as JSON (`{"message": "..."}`) and forwards them to workers.
  - Relays worker replies back to clients.
- **UDP variant** adds:
  - Thread pool + queue to buffer work.
  - Simple linked-list queue (`queue.h/.c`) protected by a mutex/condition variable.

### **Worker Side (Python)**
- Example: [`worker.py`](worker.py)
  - Minimal UDP server that:
    - Listens on assigned port.
    - Receives a message.
    - Sends back a canned response (placeholder for model output).
  - Contains commented hints for parsing JSON and returning actual predictions.
- The C server also supports [`worker_predictor.py`](worker_predictor.py) for real model-serving.
- **Defaults:**
  - Server port: `6160`
  - Worker base port: `9001`
  - Worker count: `3`
- Swap between TCP/UDP by running the corresponding server binary.

---

## Example

Below is an example of `ml-model-stresser` in action:

![Example Run](resources/readme/example.png)

---
