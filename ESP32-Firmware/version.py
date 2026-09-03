import subprocess
Import ("env")

try:
    v = subprocess.check_output(["git", "describe", "--tags"]).decode("utf-8").strip()
except Exception as e:
    v = "unknown"
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(v))])