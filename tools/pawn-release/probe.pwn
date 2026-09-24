#define ACTOR_SELF 1
#include <usum>

#if cellbits != 32 || cellmax != 2147483647 || cellmin != (-2147483647 - 1)
    #error Incorrect target cell limits
#endif

main()
{
    if (FlagGet(123)) {
        WorkSet(456, 7);
    }
    WaitFrames(1);
    return 0;
}
