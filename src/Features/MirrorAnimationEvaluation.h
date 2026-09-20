#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace MirrorAnimationEvaluation
{

    struct Callback
    {
        struct Table {void (*destroy)(Callback*,unsigned);void (*invoke)(Callback*);};
        static void Destroy(Callback*,unsigned) noexcept {}
        static void Invoke(Callback* self) {
            if (!self || self->called || !self->flush) return;
            self->called=true;
            self->accepted=self->flush(self->state);
        }
        static inline const Table methods{Destroy,Invoke};
        const Table* table{&methods};
        void* state{};
        bool (*flush)(void*){};
        bool called{},accepted{};
    };
    static_assert(std::is_trivially_destructible_v<Callback>);
    static_assert(offsetof(Callback::Table,invoke)==8);
    inline void* Install(void* privateData,Callback& callback) noexcept {
        void* previous{};std::memcpy(&previous,static_cast<std::byte*>(privateData)+0x10,sizeof(previous));
        auto* replacement=&callback;std::memcpy(static_cast<std::byte*>(privateData)+0x10,&replacement,sizeof(replacement));return previous;
    }
    inline void Restore(void* privateData,void* previous) noexcept {
        std::memcpy(static_cast<std::byte*>(privateData)+0x10,&previous,sizeof(previous));
    }
}
