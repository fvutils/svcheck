"""Agent skills shipped with svcheck.

Each function here is an ``agent.skills`` entry point (setup.py). A consumer
such as IVPM calls it and links the directory it returns into
``.claude/skills/``, ``.agents/skills/`` and so on. IVPM names the skill
after the entry point, so there is one entry point per skill, and each name
is the skill's directory and its ``SKILL.md`` ``name``.

This module imports ``os`` only, so discovery does not need the native
executable. After pssparser's ``pssparser.skills`` (svcheck-design §3.1).
"""
import os

#: The skills the wheel carries, under svcheck/share/skills/<name>.
SHIPPED = ("svcheck-rules",)


def _skill_dir(name: str) -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    installed = os.path.join(here, "share", "skills", name)
    if os.path.isfile(os.path.join(installed, "SKILL.md")):
        return installed
    # A checkout or an editable install: <repo>/skills/<name>.
    return os.path.normpath(os.path.join(here, "..", "..", "skills", name))


def rules_skill() -> str:
    return _skill_dir("svcheck-rules")
