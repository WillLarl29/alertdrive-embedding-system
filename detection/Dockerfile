# Python 3.12: mediapipe<=0.10.21 (API "solutions" que usa este script) nunca
# se compilo para 3.13+, ver detection/requirements.txt.
FROM python:3.12-slim

# libgl1/libglib2.0-0: dependencias nativas que mediapipe sigue tocando en
# tiempo de ejecucion aunque se use opencv-python-headless.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgl1 \
    libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements-docker.txt .
# Torch CPU-only desde el indice oficial: el paquete "torch" de PyPI para Linux
# arrastra varios GB de dependencias CUDA (nvidia-*) que no se usan aqui.
RUN pip install --no-cache-dir torch torchvision --index-url https://download.pytorch.org/whl/cpu
RUN pip install --no-cache-dir -r requirements-docker.txt

COPY deteccion_esp32cam.py .
COPY model/eye_cnn_best.pth model/eye_cnn_best.pth

ENV HEADLESS=1
# Sin esto, print() queda bufferizado y "docker compose logs" no muestra nada en vivo.
ENV PYTHONUNBUFFERED=1

CMD ["python", "deteccion_esp32cam.py"]
