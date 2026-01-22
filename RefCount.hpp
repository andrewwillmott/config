//
// RefCount.hpp
//
// Intrusive MS-style ref count system
//
// Andrew Willmott
//

#ifndef HL_REF_COUNT_H
#define HL_REF_COUNT_H

#include "Defs.hpp"

#if defined(HL_WINDOWS) || defined(__GNUC__) || defined(HL_NO_C11_ATOMIC)
    // We prefer _Atomic if possible as <atomic> is 5500+ lines of include
    #include <atomic>  // MVSC hasn't implemented _Atomic, g++ seems to now exclude it
    #ifndef _Atomic
        #define _Atomic(TYPE) std::atomic<TYPE>
    #endif
#endif

namespace HL
{
    //
    // A reference-counted class is any class that supports the following API:
    //
    //      int AddRef() const;
    //      int Release() const;
    //
    // Adding a reference should increment some internal reference counter,
    // and removing a reference should decrement that counter, and destroy
    // the object if there are no more external references.
    //
    // The RefCounted class below provides a basic implementation of this API,
    // but more sophisticated variants are possible.
    //
    // Any such class can be used with AutoRef<T> to safely track object
    // lifetimes. You should rarely need to call AddRef/Release
    // manually, indeed doing so invariably leads to bugs.

    //
    // Basic intrusive reference-counting smart pointer
    //

    template<class T> class AutoRef
    {
    public:
        typedef T* tPointer;

        AutoRef();
        AutoRef(T* object);
        AutoRef(const AutoRef& rhs);
        AutoRef(AutoRef&& rhs);

        ~AutoRef();

        void operator = (T* object);
        void operator = (const AutoRef& rhs);

        T& operator  *() const;
        T* operator ->() const;
        operator T*() const;

        T*   Acquire();   // Gives up ownership of currently held object and returns a pointer to it
        void Reset();     // Release current object and reset to the nullptr

        T* const& AsPtrRef() const;  // For use in converting AutoRef arrays to equivalent pointer arrays

    protected:
        T* mObject = nullptr;
    };


    //
    // Reference-counted mix-ins
    //

    template<class T> class RefCountedT
    {
    public:
        typedef RefCountedT<T> RefCountBase;

        RefCountedT()                    {}
        RefCountedT(const RefCountedT&)  {}  // we should never transfer the refcount to a new object, it will lead to leaks.
        virtual ~RefCountedT()           {}

        int AddRef() const;      // Adds a reference to 'this' and returns the new count
        int Release() const;     // Removes a reference from 'this' and returns the new count

        int RefCount() const;    // Current reference count

        void operator=(const RefCountedT&) {}  // we should never transfer the refcount to a new object, it will lead to leaks.

    protected:
        mutable T mRefCount = 0;
    };

    typedef _Atomic(int) MTInt;

    class RefCountedST : public RefCountedT<int  > { public: using RefCountedT<int  >::RefCountedT; };
    class RefCountedMT : public RefCountedT<MTInt> { public: using RefCountedT<MTInt>::RefCountedT; };

#ifdef HL_REF_COUNTED_MT
    typedef RefCountedMT RefCounted;
#else
    typedef RefCountedST RefCounted;  // default
#endif

    //
    // Reference-counted interface
    //

    struct IRefCounted
    {
        virtual int AddRef() const = 0;
        virtual int Release() const = 0;
    };
    typedef AutoRef<IRefCounted> AutoIRef;


    //
    // AsClass helper
    //

    // These routines assume the class supports a method like this:
    //    virtual void* AsClass(ClassID classID) const = 0;  // Returns given class interface if supported, or nullptr if not,
    // and declare a class ID like this:
    //    enum IDS : ClassID  { kClassID = 'blah' };

    typedef uint32_t ClassID;

    template<class T_DST, class T_SRC> const T_DST* AsClass(const T_SRC* object);  // Returns source object cast to the given destination class if supported, nullptr if not
    template<class T_DST, class T_SRC> T_DST*       AsClass(T_SRC* object);        // Returns source object cast to the given destination class if supported, nullptr if not

    //
    // AsPtrArray
    //

    // These can be used to convert arrays of references into the equivalent arrays of pointers. E.g.,
    //
    //      vector<Item> refs; Apply(refs.size(),   AsPtrArray(refs));
    //      Item refs[2];      Apply(HL_SIZE(refs), AsPtrArray(refs));
    //
    // Note that the called function must take a constant array of pointers, as
    // we can't have it changing the AutoRef's contents out from under it. So,
    //
    //      void Apply(size_t n, Item*       const items[]);   // can modify *items[i] but not items[i]
    //      void Apply(size_t n, Item const* const items[]);   // *items[i] is const.
    //
    template<class CONTAINER> typename CONTAINER::value_type::tPointer const* AsPtrArray(const CONTAINER& c);
    template<class AUTO_REF>  typename              AUTO_REF::tPointer const* AsPtrArray(AUTO_REF* const ref);

    //
    // Macros
    //

    // Use to make an interface ref-countable: class ICow { public: ... I_REF_COUNTED_DECL; }
    #define I_REF_COUNTED_DECL \
        virtual int AddRef()  const = 0;  \
        virtual int Release() const = 0

    // Use to implement a ref-countable interface : class Cow : public ICow, public RefCounted { public: ... I_REF_COUNTED_IMPL; }
    #define I_REF_COUNTED_IMPL \
        int AddRef()  const override { return RefCountBase::AddRef();  } \
        int Release() const override { return RefCountBase::Release(); }

    // Use to implement a ref-countable concrete class : class Cow : public RefCounted { public: ... REF_COUNTED_IMPL; }
    #define REF_COUNTED_IMPL \
        int AddRef()  const { return RefCountBase::AddRef();  } \
        int Release() const { return RefCountBase::Release(); }

    #define I_AS_CLASS_DECL \
        virtual void* AsClass(ClassID classID) const = 0;
    // Use this macro to fill in a default AsClass() implementation supporting only the concrete class.
    #define I_AS_CLASS_IMPL(M_CLASS) \
        void* AsClass(ClassID classID) const override { if (classID == M_CLASS::kClassID) return (M_CLASS*) this; return nullptr; }


    //
    // Inlines
    //

    // RefCountedT
    template<class T> inline int RefCountedT<T>::AddRef() const
    {
        return ++mRefCount;
    }

    template<class T> inline int RefCountedT<T>::Release() const
    {
        int newRefCount = --mRefCount;

        HL_ASSERT_F(newRefCount >= 0, "Over-Release of object");

        if (newRefCount == 0)
        {
            delete this;
        }

        return newRefCount;
    }

    template<class T> inline int RefCountedT<T>::RefCount() const
    {
        return mRefCount;
    }

    // AutoRef
    template<class T> inline AutoRef<T>::AutoRef() : mObject(0)
    {}

    template<class T> inline AutoRef<T>::AutoRef(T* object) : mObject(object)
    {
        if (mObject)
            mObject->AddRef();
    }

    template<class T> inline AutoRef<T>::AutoRef(const AutoRef& rhs) : mObject(rhs.mObject)
    {
        if (mObject)
            mObject->AddRef();
    }

    template<class T> inline AutoRef<T>::AutoRef(AutoRef&& rhs) : mObject(rhs.mObject)
    {
        rhs.mObject = nullptr;
    }

    template<class T> inline AutoRef<T>::~AutoRef()
    {
        if (mObject)
            mObject->Release();
    }

    template<class T> inline void AutoRef<T>::operator=(T* object)
    {
        if (mObject == object)
            return;

        if (mObject)
            mObject->Release();

        mObject = object;

        if (mObject)
            mObject->AddRef();
    }

    template<class T> inline void AutoRef<T>::operator=(const AutoRef& rhs)
    {
        operator=(rhs.mObject);
    }

    template<class T> inline T& AutoRef<T>::operator*() const
    {
        return *mObject;
    }
    template<class T> inline T* AutoRef<T>::operator->() const
    {
        return mObject;
    }
    template<class T> inline AutoRef<T>::operator T*() const
    {
        return mObject;
    }

    template<class T> inline T* AutoRef<T>::Acquire()
    {
        T* result = mObject;
        mObject = 0;
        return result;
    }

    template<class T> inline void AutoRef<T>::Reset()
    {
        if (mObject)
            mObject->Release();
        mObject = nullptr;
    }

    template<class T> inline T* const& AutoRef<T>::AsPtrRef() const
    {
        return mObject;
    }

    // AsClass
    template<class T_DST, class T_SRC> const T_DST* AsClass(const T_SRC* object)
    {
        if (object)
            return static_cast<T_DST*>(object->AsClass(T_DST::kClassID));
        return nullptr;
    }

    template<class T_DST, class T_SRC> T_DST* AsClass(T_SRC* object)
    {
        if (object)
            return static_cast<T_DST*>(object->AsClass(T_DST::kClassID));
        return nullptr;
    }

    template<class T_DST, class T_SRC> const T_DST* AsClass(const AutoRef<const T_SRC>& object)
    {
        if (object)
            return static_cast<T_DST*>(object->AsClass(T_DST::kClassID));
        return nullptr;
    }

    template<class T_DST, class T_SRC> T_DST* AsClass(const AutoRef<T_SRC>& object)
    {
        if (object)
            return static_cast<T_DST*>(object->AsClass(T_DST::kClassID));
        return nullptr;
    }

    // AsPtrArray
    template<class T_CONTAINER> typename T_CONTAINER::value_type::tPointer const* AsPtrArray(const T_CONTAINER& c)
    {
        return &c[0].AsPtrRef();
    }
    template<class T_REF> typename T_REF::tPointer const* AsPtrArray(T_REF* const ref)
    {
        return &ref->AsPtrRef();
    }
}

#endif
