"""Build the vendored interpreter with the Teensy runtime adapter."""
Import("env")
from os.path import join
root = join(env.subst("$PROJECT_DIR"), "components", "micropython_embed")
engine = join(root, "micropython_embed")
env.Append(CPPPATH=[root, engine, join(engine, "port")])
env.BuildSources(join(env.subst("$BUILD_DIR"), "micropython"), engine,
                 src_filter=["-<*>", "+<py/*.c>", "+<extmod/*.c>",
                             "+<shared/runtime/*.c>", "+<port/solaros_file.c>"])

if env.subst("$PIOENV") == "teensy41_audio":
    env.BuildSources(join(env.subst("$BUILD_DIR"), "minimp3"),
                     join(env.subst("$PROJECT_DIR"), "components", "minimp3"),
                     src_filter=["+<minimp3_impl.c>"])
