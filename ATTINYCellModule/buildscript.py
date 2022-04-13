Import("env")

platform = env.PioPlatform()

#print(env)
#print(platform)

my_flags = env.ParseFlags(env['BUILD_FLAGS'])
#print(my_flags)
defines = {k[0]: k[1] for k in my_flags.get("CPPDEFINES") if isinstance(k,(tuple,list))}
#print(defines.get("DIYBMSMODULEVERSION"))

efuse=hex(int(env.GetProjectOption("board_fuses.efuse"), 2)).upper()[2:4]
hfuse=hex(int(env.GetProjectOption("board_fuses.hfuse"), 2)).upper()[2:4]
lfuse=hex(int(env.GetProjectOption("board_fuses.lfuse"), 2)).upper()[2:4]

env.Replace(PROGNAME="module_fw_%s_%s_%s%s_e%s_h%s_l%s" % (env["PIOENV"],env.get("BOARD"), defines.get("DIYBMSMODULEVERSION"), "_SWAPR19R20" if "SWAPR19R20" in defines else "", efuse, hfuse, lfuse ))

