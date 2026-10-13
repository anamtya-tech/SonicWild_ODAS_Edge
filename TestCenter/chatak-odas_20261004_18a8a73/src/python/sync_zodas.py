import sys
import os
import subprocess
import time
from datetime import datetime
import io
import shutil


def create_session_folder(base_path, session_prefix):
    now = datetime.now()
    timestamp = now.strftime(f"{session_prefix}_%Y-%m-%d_%H-%M")
    folder_path = os.path.join(base_path, timestamp)
    os.makedirs(folder_path, exist_ok=True)
    return folder_path, timestamp

def sync_zodas(fS, hopSize, nBits, nChannels, audioRecordPath, session_prefix, config_file_path, parent_pid):
    print("ZODAS Sync started...")
    print(f" Sample Rate     : {fS}")
    print(f" Hop Size        : {hopSize}")
    print(f" Bit Depth       : {nBits}")
    print(f" Channels        : {nChannels}")
    print(f" Record Path     : {audioRecordPath}")
    print(f" Parent PID      : {parent_pid}")

    # Create readable timestamped folder
    now = datetime.now()
    folder_name = now.strftime(f"{session_prefix}_%Y-%m-%d_%H-%M")
    full_path = os.path.join(audioRecordPath, folder_name)
    os.makedirs(full_path, exist_ok=True)

    # File naming
    base_filename = folder_name
    audio_file_path = os.path.join(full_path, f"{base_filename}.raw")
    timestamp_file_path = os.path.join(full_path, f"{base_filename}.txt")
    session_cfg_path = os.path.join(full_path, f"{base_filename}.cfg")

    # Copy active ODAS config used for this run into the session folder
    try:
        shutil.copy(config_file_path, session_cfg_path)
        print(f"Copied config file to {session_cfg_path}")
    except Exception as e:
        print(f"Failed to copy config file: {e}")

    # ✅ Copy latlong.txt into the new folder with timestamped name
    src_latlong = "/home/chatak/ChatakGUI/config/latlong.txt"
    dest_latlong = os.path.join(full_path, f"{session_prefix}_{now.strftime('%Y-%m-%d_%H-%M')}_latlong.txt")
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

    with open(audio_file_path, "wb") as audio_file, open(timestamp_file_path, "w") as ts_file:
        ts_file.write(f"Recording started at: {now.isoformat()}\n")
        ts_file.write(f"Sample Rate: {fS}\nHop Size: {hopSize}\nBit Depth: {nBits}\nChannels: {nChannels}\nParent PID: {parent_pid}\n\n")

        try:
            while True:
                frame = reader.read(frame_bytes)
                if not frame:
                    break

                audio_file.write(frame)
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
