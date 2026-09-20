#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MirrorCloneControllers
{
    template <class Object>
    class Scope
    {
        using Controller = std::remove_cvref_t<decltype(std::declval<Object>().controllers)>;
        struct Entry { Object* owner; Controller controller; };
        std::vector<Entry> entries_;

    public:
        template <class Children>
        Scope(Object& root, Children&& children, std::size_t maxObjects = 16384)
        {
            std::vector<Object*> pending;
            std::unordered_set<Object*> seen;
            auto enqueue = [&](Object* object) {
                if (!object || !seen.insert(object).second) return;
                if (seen.size() > maxObjects) throw std::length_error("mirror clone source tree exceeds bound");
                pending.push_back(object);
            };
            enqueue(std::addressof(root));
            for (std::size_t index = 0; index < pending.size(); ++index) {
                auto* object = pending[index];
                if (object->controllers) entries_.push_back({ object, {} });
                children(*object, enqueue);
            }

            for (auto& entry : entries_) entry.controller = std::move(entry.owner->controllers);
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        ~Scope() { Restore(); }

        std::size_t Count() const noexcept { return entries_.size(); }
        void Restore() noexcept
        {
            for (auto& entry : entries_) {
                if (entry.controller) entry.owner->controllers = std::move(entry.controller);
            }
        }
    };

    namespace detail
    {

        template <class Guard, class Clone>
        auto Invoke(Guard* guard, Clone* clone) -> decltype((*clone)())
        {
            __try { return (*clone)(); }
            __finally { guard->Restore(); }
        }
    }

    template <class Object, class Children, class Clone>
    auto WithoutControllers(Object& root, Children&& children, Clone&& clone,
        std::size_t* count = nullptr) -> decltype(clone())
    {
        Scope<Object> guard(root, std::forward<Children>(children));
        if (count) *count = guard.Count();
        return detail::Invoke(std::addressof(guard), std::addressof(clone));
    }
}
