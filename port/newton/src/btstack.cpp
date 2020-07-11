#include <NewtonScript.h>
#include <Objects.h>

#include "SerialChipRegistry.h"

#include "log.h"

extern "C" int main(void);

extern "C"
Ref
MStart(RefArg inRcvr)
{
    main();
    return NILREF;
}
