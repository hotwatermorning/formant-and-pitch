#pragma once
#include "Prefix.h"

NS_HWM_BEGIN

template<class T,
         class TypeOfCriticalSectionToUse = juce::DummyCriticalSection,
         int minimumAllocatedSize = 0
>
class ReferenceableArray
:   private juce::Array<T, TypeOfCriticalSectionToUse, minimumAllocatedSize>
{
public:
    using base_type = juce::Array<T, TypeOfCriticalSectionToUse, minimumAllocatedSize>;

    template<class ...Args>
    explicit
    ReferenceableArray(Args&& ...args)
    :   base_type(std::forward<Args>(args)...)
    {}

    base_type & base() { return static_cast<base_type &>(*this); }
    base_type const & base() const { return static_cast<base_type const &>(*this); }

    ReferenceableArray(ReferenceableArray const &rhs)
    {
        base() = rhs.base();
    }

    ReferenceableArray & operator=(ReferenceableArray const &rhs)
    {
        base() = rhs.base();
        return *this;
    }

    ReferenceableArray(ReferenceableArray &&rhs)
    {
        base() = std::move(rhs.base());
    }

    ReferenceableArray & operator=(ReferenceableArray &&rhs)
    {
        if(this == &rhs) { return *this; }

        base() = std::move(rhs.base());
        return *this;
    }

    using base_type::clear;
    using base_type::clearQuick;
    using base_type::fill;
    using base_type::size;
    using base_type::isEmpty;
    using base_type::getUnchecked;
    using base_type::getReference;
    using base_type::getFirst;
    using base_type::getLast;
    using base_type::getRawDataPointer;
    using base_type::begin;
    using base_type::end;
    using base_type::data;
    using base_type::indexOf;
    using base_type::contains;
    using base_type::add;
    using base_type::insert;
    using base_type::insertMultiple;
    using base_type::insertArray;
    using base_type::addIfNotAlreadyThere;
    using base_type::set;
    using base_type::setUnchecked;
    using base_type::addArray;
    using base_type::addNullTerminatedArray;
    using base_type::swapWith;
    using base_type::resize;
    using base_type::addSorted;
    using base_type::addUsingDefaultSort;
    using base_type::remove;
    using base_type::removeAndReturn;
    using base_type::removeFirstMatchingValue;
    using base_type::removeAllInstancesOf;
    using base_type::removeIf;
    using base_type::removeRange;
    using base_type::removeLast;
    using base_type::removeValuesIn;
    using base_type::removeValuesNotIn;
    using base_type::swap;
    using base_type::move;
    using base_type::minimiseStorageOverheads;
    using base_type::ensureStorageAllocated;
    using base_type::sort;
    using base_type::getLock;

    T & operator[](int index)
    {
        return base().getReference(index);
    }

    T const & operator[](int index) const
    {
        return base().getReference(index);
    }
};

NS_HWM_END
