# Tools shipped inside !VanillaTD. Built by riscos/scripts/deploy-td.sh; they
# also build natively (cmake -S riscos/tools -B build/tools) for testing.
cmake_minimum_required(VERSION 3.25)
project(RiscosTools C)

add_executable(vcprep vcprep/vcprep.c vcprep/blast.c)

if(RISCOS)
    riscos_make_aif(vcprep)
endif()
