#if !defined(_XCP_INFRA_MACRO_H_INCLUDED_)
#define _XCP_INFRA_MACRO_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


//
// Macros for defaulted/disabled copy/move constructor
//
#define XCP_DISABLE_COPY_CONSTRUCTOR(_Class_) \
    _Class_(const _Class_&) = delete; \
    _Class_& operator =(const _Class_&) = delete;

#define XCP_DISABLE_MOVE_CONSTRUCTOR(_Class_) \
    _Class_(_Class_&&) = delete; \
    _Class_& operator =(_Class_&&) = delete;

#define XCP_DEFAULT_COPY_CONSTRUCTOR(_Class_) \
    _Class_(const _Class_&) = default; \
    _Class_& operator =(const _Class_&) = default;

#define XCP_DEFAULT_MOVE_CONSTRUCTOR(_Class_) \
    _Class_(_Class_&&) noexcept = default; \
    _Class_& operator =(_Class_&&) noexcept = default;


//
// Macros for defaulted copy/move constructor
//
#define XCP_DEFAULT_COPY_ASSIGN(_Class_) \
    _Class_& operator=(const _Class_& other) noexcept \
    { \
        if (this == &other) return *this; \
        this->~_Class_(); \
        new (this) _Class_(other); \
        return *this; \
    }

#define XCP_DEFAULT_MOVE_ASSIGN(_Class_) \
    _Class_& operator=(_Class_&& other) noexcept \
    { \
        this->~_Class_(); \
        new (this) _Class_(std::move(other)); \
        return *this; \
    }

#define XCP_MOVE_FROM_OTHER(_Field_, ...) \
    do { \
        /*static_assert(std::is_trivially_destructible_v<decltype(this->_Field_)>);*/ \
        this->_Field_ = std::move(other._Field_); \
        other._Field_ = decltype(_Field_)(__VA_ARGS__); \
    } while(false)

#define XCP_DEFAULT_MOVE_BY_SWAP(_Class_) \
    \
    XCP_DEFAULT_MOVE_ASSIGN(_Class_) \
    \
    _Class_(_Class_&& other) noexcept \
        : _Class_() \
    { \
        swap(other); \
    } \
    \
    void swap(_Class_& other) noexcept


//
// Serialization
//
#define XCP_DEFAULT_SERIALIZATION(...) \
    friend class cereal::access; \
    \
    template<typename Archive> \
    void serialize(Archive& ar) \
    { \
        ar(__VA_ARGS__); \
    }

#define XCP_DEFAULT_SERIALIZATION_WITH_BASE(_Base_, ...) \
    friend class cereal::access; \
    \
    template<typename Archive> \
    void serialize(Archive& ar) \
    { \
        ar(cereal::base_class<_Base_>(this), __VA_ARGS__); \
    }


//
// Helper macros
//
#define JUST(...)       __VA_ARGS__

#define __TEXTIFY(_X_)  #_X_
#define TEXTIFY(_X_)    __TEXTIFY(_X_)


#endif  // !defined(_XCP_INFRA_MACRO_H_INCLUDED_)
