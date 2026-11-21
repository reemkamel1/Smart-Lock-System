
import os

os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["OMP_NUM_THREADS"] = "1"
os.environ["MKL_NUM_THREADS"] = "1"
os.environ["HF_HUB_DISABLE_XET"] = "1"

import sys
import re
import time
import serial

import numpy as np
import sounddevice as sd
import torch

from faster_whisper import WhisperModel
from speechbrain.inference.speaker import EncoderClassifier
from speechbrain.utils.fetching import LocalStrategy


# ---------------- Settings ----------------

PHRASE_TO_SAY = "open the door"

# Initial threshold - must be calibrated with genuine/impostor tests
THRESH = 0.45

MIN_PEAK = 0.10
MAX_PEAK = 0.98

# USB Serial communication with ESP32
SERIAL_PORT = "COM3"
SERIAL_BAUD = 115200

FS = 16000
DUR = 4

ENROLL_COUNT = 8
ATTEMPTS = 3

TEMPLATE = "voiceprint.npy"


# ---------------- Load Models ----------------

print("Loading Faster-Whisper model (base / CPU / INT8)...")

whisper_model = WhisperModel(
    "base",
    device="cpu",
    compute_type="int8"
)

print("Faster-Whisper loaded successfully!")

print("Loading ECAPA-TDNN speaker model...")

speaker_model = EncoderClassifier.from_hparams(
    source="speechbrain/spkrec-ecapa-voxceleb",
    savedir="ecapa_model",
    run_opts={"device": "cpu"},
    local_strategy=LocalStrategy.COPY,
)

print("ECAPA-TDNN loaded successfully!")


# ---------------- Recording ----------------

def record(wait_for_enter=True):

    if wait_for_enter:
        input(
            f"\nPress Enter, wait for 'Speak now', "
            f"then say '{PHRASE_TO_SAY}'... "
        )

    print(f"\n>>> Speak now: {PHRASE_TO_SAY}")

    try:
        audio = sd.rec(
            int(DUR * FS),
            samplerate=FS,
            channels=1,
            dtype="float32"
        )

        sd.wait()

    except Exception as e:
        print("Recording Error:", e)
        return None

    audio = audio.flatten()

    if audio.size == 0:
        print("Recording is empty")
        return None

    peak = float(np.max(np.abs(audio)))

    print(
        "Mic level:",
        round(peak, 3),
        "(good range: 0.15 - 0.9)"
    )

    if peak < MIN_PEAK:
        print("Too quiet - no speech detected")
        return None

    if peak > MAX_PEAK:
        print("Too loud - clipping")
        return None

    audio = audio / peak * 0.9
    full = audio.copy()

    env = np.convolve(
        np.abs(audio),
        np.ones(400) / 400,
        mode="same"
    )

    if env.max() == 0:
        print("No speech detected")
        return None

    idx = np.where(env > 0.15 * env.max())[0]

    if len(idx) == 0:
        print("No speech region detected")
        return None

    start = max(int(idx[0]) - 4800, 0)
    end = min(int(idx[-1]) + 4800, len(audio))

    speech = audio[start:end]

    if len(speech) < FS // 2:
        print("Speech too short")
        return None

    return full, speech


# ---------------- Text Processing ----------------

def normalize_text(text):

    text = text.lower().strip()
    text = re.sub(r"[^a-z\s]", "", text)
    text = " ".join(text.split())

    return text


# ---------------- Check 1: Phrase Recognition ----------------

def check_word(full):

    try:
        segments, info = whisper_model.transcribe(
            full.astype(np.float32),
            language="en",
            beam_size=5,
            vad_filter=True,
            condition_on_previous_text=False
        )

        transcript = " ".join(
            segment.text.strip()
            for segment in segments
        )

    except Exception as e:
        print("Speech recognition error:", e)
        return False

    heard = normalize_text(transcript)
    expected = normalize_text(PHRASE_TO_SAY)

    print("Detected language:", info.language)
    print("Heard:", heard)

    if heard == expected:
        print("Phrase: OK")
        return True

    print("Phrase: WRONG - expected:", expected)
    return False


# ---------------- Speaker Embedding ----------------

def embedding(speech):

    signal = torch.from_numpy(
        speech.astype(np.float32)
    ).float().unsqueeze(0)

    with torch.no_grad():
        emb = speaker_model.encode_batch(
            signal
        ).squeeze().cpu().numpy()

    norm = np.linalg.norm(emb)

    if norm == 0:
        raise ValueError("Invalid voice embedding")

    return emb / norm


# ---------------- Check 2: Speaker Verification ----------------

def check_voice(speech):

    if not os.path.exists(TEMPLATE):
        print("Voiceprint file not found. Enroll first.")
        return False

    template = np.load(TEMPLATE)

    current_embedding = embedding(speech)

    score = float(np.dot(current_embedding, template))

    print(
        "Voice similarity =",
        round(score, 2),
        "| threshold =",
        THRESH
    )

    return score >= THRESH


# ---------------- Enrollment ----------------

def enroll():

    print(
        f"Enrollment: say '{PHRASE_TO_SAY}' "
        f"{ENROLL_COUNT} times"
    )

    embs = []

    while len(embs) < ENROLL_COUNT:

        print(f"\nRecording {len(embs) + 1}/{ENROLL_COUNT}")

        rec = record(wait_for_enter=True)

        if rec is None:
            print("Try again")
            continue

        try:
            embs.append(embedding(rec[1]))

        except Exception as e:
            print("Embedding Error:", e)
            print("Try again")

    template = np.mean(embs, axis=0)
    template_norm = np.linalg.norm(template)

    if template_norm == 0:
        print("Enrollment failed: invalid voiceprint")
        return False

    template /= template_norm
    np.save(TEMPLATE, template)

    scores = []

    for i in range(len(embs)):

        others = np.mean(
            [
                embs[j]
                for j in range(len(embs))
                if j != i
            ],
            axis=0
        )

        others_norm = np.linalg.norm(others)

        if others_norm == 0:
            continue

        others /= others_norm
        scores.append(float(np.dot(embs[i], others)))

    if scores:
        print(
            "\nExpected similarity:",
            [round(x, 2) for x in scores]
        )

        print("Lowest =", round(min(scores), 2))

    print("Voiceprint saved successfully!")
    return True


# ---------------- Voice Verification ----------------

def verify():

    for i in range(ATTEMPTS):

        print(f"\nAttempt {i + 1}/{ATTEMPTS}")

        # No Enter key required in automatic mode
        rec = record(wait_for_enter=False)

        if rec is None:
            continue

        full, speech = rec

        word_ok = check_word(full)

        try:
            voice_ok = check_voice(speech)

        except Exception as e:
            print("Voice verification error:", e)
            voice_ok = False

        print(
            "Phrase:", "OK" if word_ok else "WRONG",
            "| Voice:", "OK" if voice_ok else "WRONG"
        )

        # Both checks must pass
        if word_ok and voice_ok:
            return True

    return False


# ---------------- USB Serial Server ----------------

def serial_server():

    print("\nOpening ESP32 USB Serial...")
    print("Port:", SERIAL_PORT)
    print("Baud:", SERIAL_BAUD)

    try:
        with serial.Serial(
            port=SERIAL_PORT,
            baudrate=SERIAL_BAUD,
            timeout=0.2,
            write_timeout=3
        ) as esp:

            # ESP32 may restart when COM port opens
            time.sleep(2)

            esp.reset_input_buffer()

            print("\nESP32 connection established!")
            print("Waiting for verification request...")
            print("Press Ctrl+C to stop.\n")

            while True:

                message = esp.readline().decode(
                    "utf-8",
                    errors="ignore"
                ).strip()

                if message != "V":
                    continue

                print("\n================================")
                print("Verification request received!")
                print("================================")

                result = verify()

                if result:
                    response = b"1\n"
                    print("\nRESULT: MATCH")
                else:
                    response = b"0\n"
                    print("\nRESULT: NO MATCH")

                esp.write(response)
                esp.flush()

                print(
                    "Result sent to ESP32:",
                    response.decode().strip()
                )

                print("\nWaiting for next request...")

    except serial.SerialException as e:
        print("\nSerial Connection Error:", e)
        print("Check COM port and close Arduino Serial Monitor.")

    except KeyboardInterrupt:
        print("\nSerial server stopped.")


# ---------------- Test Mode ----------------

def test_mode():

    if not os.path.exists(TEMPLATE):
        print("No voiceprint found.")
        print("Run enrollment first: python smart.py enroll")
        return

    print("\nStarting test mode...")

    for i in range(5):

        print(f"\nTest {i + 1}/5")

        rec = record(wait_for_enter=True)

        if rec is None:
            continue

        full, speech = rec

        word_ok = check_word(full)

        try:
            voice_ok = check_voice(speech)

        except Exception as e:
            print("Voice verification error:", e)
            voice_ok = False

        print(
            "Phrase:", "OK" if word_ok else "WRONG",
            "| Voice:", "OK" if voice_ok else "WRONG"
        )


# ---------------- Main ----------------

def main():

    mode = sys.argv[1].lower() if len(sys.argv) > 1 else "run"

    if mode not in ["run", "enroll", "test"]:
        print("Invalid mode.")
        print("Use: python smart.py [run/enroll/test]")
        return

    if mode == "test" and not os.path.exists(TEMPLATE):
        print("No voiceprint found.")
        print("Run enrollment first: python smart.py enroll")
        return

    if mode == "enroll":
        enroll()
        return

    if not os.path.exists(TEMPLATE):
        print("No voiceprint found. Starting enrollment...")
        success = enroll()

        if not success:
            return

    if mode == "test":
        test_mode()
        return

    print("\nStarting ESP32 serial verification server...")
    serial_server()


if __name__ == "__main__":

    try:
        main()

    except KeyboardInterrupt:
        print("\nProgram stopped by user.")

    except Exception as e:
        print("\nUnexpected Error:", e)
