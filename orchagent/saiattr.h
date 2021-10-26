#pragma once

extern "C"
{
#include <sai.h>
#include <saimetadata.h>
}

#include <string>

class SaiAttrWrapper
{
public:
    SaiAttrWrapper() = default;

    SaiAttrWrapper(sai_object_type_t objectType, const sai_attribute_t& attr);
    SaiAttrWrapper(const SaiAttrWrapper& other);
    SaiAttrWrapper(const SaiAttrWrapper&& other);
    SaiAttrWrapper& operator=(const SaiAttrWrapper& other);
    SaiAttrWrapper& operator=(const SaiAttrWrapper&& other);
    virtual ~SaiAttrWrapper();

    bool operator<(const SaiAttrWrapper& other) const;

    const sai_attribute_t& getSaiAttr() const;
    std::string toString() const;
    sai_attr_id_t getAttrId() const;

private:

    void init(
        sai_object_type_t objectType,
        const sai_attr_metadata_t& meta,
        const sai_attribute_t& attr);
    void swap(const SaiAttrWrapper&& other);

    sai_object_type_t m_objectType;
    const sai_attr_metadata_t* m_meta;
    sai_attribute_t m_attr;
    std::string m_serializedAttr;
};
