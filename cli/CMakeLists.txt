add_executable(msgpack-pjsekai-gen main.c)
add_executable(msgpack_pjsekai::generator ALIAS msgpack-pjsekai-gen)

install(TARGETS msgpack-pjsekai-gen RUNTIME DESTINATION bin)
