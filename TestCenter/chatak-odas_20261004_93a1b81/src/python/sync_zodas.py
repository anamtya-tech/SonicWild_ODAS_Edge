import sys
import os
import subprocess
import time
from datetime import datetime
import io
import shutil
import csv
import signal
from contextlib import ExitStack


def stop_timing_recorder(signum, frame):
    raise KeyboardInterrupt


def read_mac_id():
    try:
        with open("/home/chatak/ChatakGUI/config/mac_id.txt", "r") as mac_file:
            return mac_file.readline().strip()
    except OSError:
        return ""


def create_session_folder(base_path, session_prefix):
    os.makedirs(base_path, exist_ok=True)

    for suffix in range(1000):
        timestamp = datetime.now().strftime(f"{session_prefix}_%Y-%m-%d_%H-%M-%S")
        if suffix:
            timestamp = f"{timestamp}_{suffix}"
        folder_path = os.path.join(base_path, timestamp)
        try:
            os.mkdir(folder_path)
            return folder_path, timestamp
        except FileExistsError:
            continue

    raise RuntimeError("Unable to create a unique recording session directory")

def sync_zodas(fS, hopSize, nBits, nChannels, audioRecordPath, session_prefix, config_file_path, parent_pid):
    print("ZODAS Sync started...")
    print(f" Sample Rate     : {fS}")
    print(f" Hop Size        : {hopSize}")
    print(f" Bit Depth       : {nBits}")
    print(f" Channels        : {nChannels}")
    print(f" Record Path     : {audioRecordPath}")
    print(f" Parent PID      : {parent_pid}")

    # Create a unique readable timestamped folder without reusing an existing session.
    full_path, folder_name = create_session_folder(audioRecordPath, session_prefix)
    now = datetime.now()

    # File naming
    base_filename = folder_name
    mac_id = read_mac_id()
    file_prefix = f"{mac_id}_" if mac_id else ""
    audio_file_path = os.path.join(full_path, f"{file_prefix}{base_filename}.raw")
    timestamp_file_path = os.path.join(full_path, f"{file_prefix}{base_filename}.txt")
    session_cfg_path = os.path.join(full_path, f"{file_prefix}{base_filename}.cfg")

    # Copy active ODAS config used for this run into the session folder
    try:
        shutil.copy(config_file_path, session_cfg_path)
        print(f"Copied config file to {session_cfg_path}")
    except Exception as e:
        print(f"Failed to copy config file: {e}")

    # ✅ Copy latlong.txt into the new folder with timestamped name
    src_latlong = "/home/chatak/ChatakGUI/config/latlong.txt"
    dest_latlong = os.path.join(full_path, f"{file_prefix}{session_prefix}_{now.strftime('%Y-%m-%d_%H-%M')}_latlong.txt")
    try:
        shutil.copy(src_latlong, dest_latlong)
        print(f"Copied latlong.txt to {dest_latlong}")
    except Exception as e:
        print(f"Failed to copy latlong.txt: {e}")

    # Frame size in bytes
    frame_bytes = hopSize * nChannels * (nBits // 8)

    # arecord command with tuned buffer and period size
    arecord_cmd = [
        "arecord",
        "-D", "plug:multi_capture",
        "-t", "raw",
        "-f", f"S{nBits}_LE",
        "-r", str(fS),
        "-c", str(nChannels),
        "--buffer-size=" + str(hopSize * 16),
        "--period-size=" + str(hopSize),
        "-q"
    ]

    print(f"Launching arecord: {' '.join(arecord_cmd)}")

    # Start arecord and stream output
    proc = subprocess.Popen(arecord_cmd, stdout=subprocess.PIPE)
    reader = io.BufferedReader(proc.stdout, buffer_size=frame_bytes * 4)

    timing_enabled = bool(os.environ.get("ODAS_TIMING_DIR"))
    if timing_enabled:
        signal.signal(signal.SIGTERM, stop_timing_recorder)

    with ExitStack() as stack:
        audio_file = stack.enter_context(open(audio_file_path, "wb"))
        ts_file = stack.enter_context(open(timestamp_file_path, "w"))
        timing_writer = None
        if timing_enabled:
            timing_path = os.path.join(full_path, f"{file_prefix}{base_filename}.write_timing.csv")
            timing_file = stack.enter_context(open(timing_path, "w", newline=""))
            timing_writer = csv.writer(timing_file)
            timing_writer.writerow([
                "frame_index", "byte_offset", "bytes", "read_start_ns",
                "write_start_ns", "write_end_ns",
            ])
        frame_index = 0
        byte_offset = 0
        ts_file.write(f"Recording started at: {now.isoformat()}\n")
        ts_file.write(f"Sample Rate: {fS}\nHop Size: {hopSize}\nBit Depth: {nBits}\nChannels: {nChannels}\nParent PID: {parent_pid}\n\n")

        try:
            while True:
                read_start_ns = time.monotonic_ns() if timing_enabled else 0
                frame = reader.read(frame_bytes)
                if not frame:
                    break

                write_start_ns = time.monotonic_ns() if timing_enabled else 0
                audio_file.write(frame)
                write_end_ns = time.monotonic_ns() if timing_enabled else 0
                if timing_writer is not None:
                    timing_writer.writerow([
                        frame_index, byte_offset, len(frame), read_start_ns,
                        write_start_ns, write_end_ns,
                    ])
                frame_index += 1
                byte_offset += len(frame)
                ts_file.write(f"{datetime.now().isoformat()}\n")

                if not os.path.exists(f"/proc/{parent_pid}"):
                    print("ZODAS process exited. Stopping arecord...")
                    proc.terminate()
                    proc.wait()
                    ts_file.write(f"Recording stopped at: {datetime.now().isoformat()}\n")
                    print("? arecord terminated successfully.")
                    break

        except KeyboardInterrupt:
            proc.terminate()
            proc.wait()
            ts_file.write(f"Recording interrupted at: {datetime.now().isoformat()}\n")
            print("? Recording interrupted by user.")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

if __name__ == "__main__":
    if len(sys.argv) != 9:
        print("Expected 8 arguments: fS hopSize nBits nChannels audioRecordPath sessionPrefix configFilePath parentPID")
        sys.exit(1)

    fS = int(sys.argv[1])
    hopSize = int(sys.argv[2])
    nBits = int(sys.argv[3])
    nChannels = int(sys.argv[4])
    audioRecordPath = sys.argv[5]
    session_prefix = sys.argv[6]
    config_file_path = sys.argv[7]
    parent_pid = int(sys.argv[8])

    sync_zodas(fS, hopSize, nBits, nChannels, audioRecordPath, session_prefix, config_file_path, parent_pid)
