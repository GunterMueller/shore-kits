
#include "tests/common/drive_stage.h"
#include "engine/core/stage_container.h"


using namespace qpipe;


/**
 *  @brief Simulates a worker thread on the specified stage.
 *
 *  @param arg A stage_t* to work on.
 */
void* drive_stage(void* arg)
{
  stage_container_t* sc = (stage_container_t*)arg;
  sc->run();

  return NULL;
}