# Tools shipped inside !VanillaTD. Built by riscos/scripts/deploy-td.sh; they
# also build natively (cmake -S riscos/tools -B build/tools) for testing.
cmake_minimum_required(VERSION 3.25)
project(RiscosTools C)

add_executable(vcprep vcprep/vcprep.c vcprep/blast.c)
# The build vcprep names in its install log: git describe, from the deploy scripts.
if(TOOLS_BUILD)
    target_compile_definitions(vcprep PRIVATE TOOLS_BUILD="${TOOLS_BUILD}")
endif()

if(RISCOS)
    riscos_make_aif(vcprep)
endif()
