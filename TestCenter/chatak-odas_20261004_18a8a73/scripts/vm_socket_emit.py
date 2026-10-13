import socket
import time
import argparse
import os

CHANNELS = 6
FRAME_SIZE = 2 * CHANNELS  # 2 bytes per int16 sample
DEFAULT_FRAME_INTERVAL = 0.01  # 10 ms fallback
HOP_SIZE = 128  # match ODAS config
FRAME_SIZE = HOP_SIZE * CHANNELS * 2  # 1536 bytes

def stream_audio(audio_path, timestamp_path, port):
    print(f" Streaming audio from: {audio_path}")
    print(f" Listening on 0.0.0.0:{port}...")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("0.0.0.0", port))
        s.listen(1)
        conn, addr = s.accept()
        print(f" ODAS connected from {addr}")

        with open(audio_path, "rb") as audio_file:
            timestamps = []
            if timestamp_path and os.path.exists(timestamp_path):
                with open(timestamp_path, "r") as ts_file:
                    timestamps = [float(line.strip()) for line in ts_file if line.strip()]
                    print(f" Loaded {len(timestamps)} timestamps")
            else:
                print(" No timestamp file found. Using 10 ms fallback.")

            frame_index = 0
            while True:
                frame = audio_file.read(FRAME_SIZE)
                if not frame:
                    break

                conn.sendall(frame)

                if timestamps and frame_index < len(timestamps) - 1:
                    delta = timestamps[frame_index + 1] - timestamps[frame_index]
                    time.sleep(max(delta, 0))
                else:
                    time.sleep(DEFAULT_FRAME_INTERVAL)

                frame_index += 1

        print(" Stream ended.")

if __name__ == "__main__":
    # python3 /home/azureuser/sodas/vm_socket_emit.py --port 10000 --audio /home/azureuser/sodas/liveSession_2025-11-08_20-54/liveSession_2025-11-08_20-54.raw
    # python /home/azureuser/sodas/vm_socket_emit.py --port 10000 --audio /home/azureuser/simulator/outputs/renders/wolf_frog_ele_20251123_011724.raw
    parser = argparse.ArgumentParser(description="Stream 6-channel audio to ODAS over TCP socket.")
    # example audio: /home/azureuser/sodas/liveSession_2025-11-08_20-54/liveSession_2025-11-08_20-54.raw
    parser.add_argument("--audio", required=True, help="Path to 6-channel raw audio file")
    parser.add_argument("--timestamps", required=False, help="Path to timestamp file (one per frame)")
    parser.add_argument("--port", type=int, default=10000, help="Port to stream on (must match ODAS config)")
    args = parser.parse_args()

    stream_audio(args.audio, args.timestamps, args.port)
