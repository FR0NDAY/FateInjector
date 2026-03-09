target_sources(fateInjector PRIVATE
main.cpp
config.cpp
FixFilePerms.cpp
inject.cpp
pch.cpp
icon.rc)

target_precompile_headers(fateInjector PRIVATE "pch.h")
