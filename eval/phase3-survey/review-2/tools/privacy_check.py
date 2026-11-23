"""privacy_check.py: counts, in each file named, the occurrences of what must not appear in files
that will be committed to a public repository: local absolute paths (a path of two or more
components that starts at the system's Users, private, tmp, home or var directory, outside a
placeholder such as <scratch>,
and the real paths of the OUT, REPO and SCRATCH directories), e-mail
addresses, and the local account's identity (the login name and its parts of four letters or
more, the words of git's user.name and the labels of git's user.email domain, from the
environment and from git config). It prints counts only, never the values or the matched text,
so that its output can be kept as a public record. Reads the files named and git's config; writes
nothing. Python 3 standard library only; for the review (review-2), not repository code.

usage: python3 -I -B privacy_check.py <file>...
"""
import os
import re
import subprocess
import sys

ABSOLUTE = re.compile(r"(?<![\w<>.~-])/(?:Users|private|tmp|home|var)/[\w.-]+")
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+")


def git_config(key):
    try:
        return subprocess.run(["git", "config", "--get", key], capture_output=True, text=True,
                              check=False).stdout.strip()
    except OSError:
        return ""


def identity_words():
    words = set()
    user = os.environ.get("USER", "")
    if user:
        words.add(user)
        words |= {p for p in re.split(r"[._-]", user) if len(p) >= 4}
    words |= {w for w in git_config("user.name").split() if len(w) >= 4}
    email = git_config("user.email")
    if "@" in email:
        words.add(email)
        words |= {p for p in email.split("@", 1)[1].split(".")[:-1] if len(p) >= 4}
    return sorted(words)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    paths = [os.path.realpath(os.environ[k]) for k in ("OUT", "REPO", "SCRATCH") if os.environ.get(k)]
    words = identity_words()
    total = 0
    for path in argv[1:]:
        text = open(path, encoding="utf-8", errors="replace").read()
        n_path = sum(text.count(p) for p in paths) + len(ABSOLUTE.findall(text))
        n_mail = len(EMAIL.findall(text))
        n_id = sum(len(re.findall(r"(?i)\b" + re.escape(w) + r"\b", text)) for w in words)
        total += n_path + n_mail + n_id
        print(f"{path}: local paths {n_path}, e-mail addresses {n_mail}, identity words {n_id}")
    print(f"{len(argv) - 1} files; {total} occurrences in all ({len(words)} identity words checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
