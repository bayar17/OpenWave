"""Offline SELinux AVC summary from bounded disposable-QEMU serial evidence.

Informational diagnostic only: a permissive AVC means SELinux would deny the
access under enforcing mode. Never translate these events into allow rules
automatically or treat absence of AVCs as successful enforcement.
"""
import re
from collections import Counter

AVC = re.compile(r"\bavc:\s+denied\b", re.I)
PERMS = re.compile(r"denied\s+\{\s*([^}]+)\s*\}", re.I)
FIELD = {
    field: re.compile(r"\b" + field + r"=([^\s]+)")
    for field in ("scontext", "tcontext", "tclass", "permissive", "comm", "path")
}


def domain(context):
    fields = context.split(":")
    return fields[2] if len(fields) >= 3 else "unparsed"


def analyze_avcs(serial: str) -> dict:
    counter = Counter()
    total = 0
    permissive = Counter()
    samples = []
    for line in serial.replace("\r\n", "\n").splitlines():
        match = AVC.search(line)
        if not match:
            continue
        total += 1
        found = {key: (regex.search(line).group(1) if regex.search(line)
                       else "unknown") for key, regex in FIELD.items()}
        permissions = PERMS.search(line)
        operations = " ".join(sorted((permissions.group(1) if permissions
                                      else "unknown").split()))
        source = domain(found["scontext"])
        target = domain(found["tcontext"])
        key = (source, target, found["tclass"], operations)
        counter[key] += 1
        permissive[found["permissive"]] += 1
        if len(samples) < 40:
            samples.append({
                "source_domain": source, "target_type": target,
                "class": found["tclass"], "permissions": operations,
                "permissive": found["permissive"],
                "command": found["comm"][:80],
                "path": found["path"][:240],
            })
    groups = [
        {"source_domain": key[0], "target_type": key[1], "class": key[2],
         "permissions": key[3], "count": count}
        for key, count in counter.most_common(100)
    ]
    return {
        "total_avc_records": total,
        "permissive_flags": dict(sorted(permissive.items())),
        "groups": groups,
        "sample_records": samples,
        "evidence_only": True,
        "auto_policy_changes": False,
    }
