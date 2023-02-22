#ifndef _FAULT_TYPES_HXX_
#define _FAULT_TYPES_HXX_

namespace nuraft {
    enum class fault_type {
        none,
        sleep,
        kill_self,
        vote_monopoly
    };
} // namespace nuraft

#endif // _FAULT_TYPES_HXX_