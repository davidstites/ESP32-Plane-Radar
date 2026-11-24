# PlatformIO extra script for the native test env: link with gcov
# instrumentation and tell tests where the recorded fixtures live.
Import("env", "projenv")

for e in (env, projenv):
    e.Append(
        LINKFLAGS=["--coverage"],
        CPPDEFINES=[
            ("ADSB_FIXTURE_DIR", e.StringifyMacro(e.subst("$PROJECT_DIR/test/fixtures"))),
        ],
    )
