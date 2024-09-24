project(snd_qf)

include_directories(${VORBIS_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${OGG_INCLUDE_DIR} "../snd_common" ".")

file(GLOB SND_QF_HEADERS
    "*.h"
	"../gameshared/q_*.h"
	"../client/snd_public.h"
)

file(GLOB SND_QF_SOURCES
    "*.c"
    "../snd_common/*.c"
    "../gameshared/q_*.c"
)

file(GLOB SND_QF_PLATFORM_SOURCES
    "../sdl/sdl_snd.c"
)

if (UNIX)
    set(SND_QF_PLATFORM_LIBRARIES ${SDL2_LIBRARY} m)
endif()

add_library(snd_qf SHARED ${SND_QF_HEADERS} ${SND_QF_SOURCES} ${SND_QF_PLATFORM_SOURCES})
target_link_libraries(snd_qf PRIVATE ${OGG_LIBRARY} ${VORBIS_LIBRARIES} ${SND_QF_PLATFORM_LIBRARIES})
qf_set_output_dir(snd_qf libs)
