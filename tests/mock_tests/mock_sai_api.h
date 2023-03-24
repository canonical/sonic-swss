#include "mock_orchagent_main.h"
#include "gmock/gmock.h"

// sai_route_api_t *old_sai_route_api;
// sai_route_api_t ut_sai_route_api;
// sai_next_hop_api_t *old_sai_next_hop_api;
// sai_next_hop_api_t ut_sai_next_hop_api;
// sai_acl_api_t *old_sai_acl_api;
// sai_acl_api_t ut_sai_acl_api;

using ::testing::Return;
using ::testing::NiceMock;

std::set<void (*)()> apply_mock_fns;
std::set<void (*)()> remove_mock_fns;

#define CREATE_PARAMS(sai_object_type) _In_ const sai_##sai_object_type##_entry_t *sai_object_type##_entry, _In_ uint32_t attr_count, _In_ const sai_attribute_t *attr_list
#define REMOVE_PARAMS(sai_object_type) _In_ const sai_##sai_object_type##_entry_t *sai_object_type##_entry
#define CREATE_ARGS(sai_object_type) sai_object_type##_entry, attr_count, attr_list
#define REMOVE_ARGS(sai_object_type) sai_object_type##_entry
#define GENERIC_CREATE_PARAMS(sai_object_type) _Out_ sai_object_id_t *sai_object_type##_id, _In_ sai_object_id_t switch_id, _In_ uint32_t attr_count, _In_ const sai_attribute_t *attr_list
#define GENERIC_REMOVE_PARAMS(sai_object_type) _In_ sai_object_id_t sai_object_type##_id
#define GENERIC_CREATE_ARGS(sai_object_type) sai_object_type##_id, switch_id, attr_count, attr_list
#define GENERIC_REMOVE_ARGS(sai_object_type) sai_object_type##_id

/*
The macro DEFINE_SAI_API_MOCK will perform the steps to mock the SAI API for the sai_object_type it is called on:
1. Create a pointer to store the original API
2. Create a new SAI_API where we can safely mock without affecting the original API
3. Define a class with mocked methods to create and remove the object type (to be used with gMock)
4. Create a pointer of the above class
5. Define two wrapper functions to create and remove the object type that has the same signature as the original SAI API function
6. Define a method to apply the mock
7. Define a method to remove the mock
*/
#define DEFINE_SAI_API_MOCK(sai_object_type)                                                                                    \
    sai_##sai_object_type##_api_t *old_sai_##sai_object_type##_api;                                                             \
    sai_##sai_object_type##_api_t ut_sai_##sai_object_type##_api;                                                               \
    class mock_sai_##sai_object_type##_api_t                                                                                    \
    {                                                                                                                           \
    public:                                                                                                                     \
        mock_sai_##sai_object_type##_api_t()                                                                                    \
        {                                                                                                                       \
            ON_CALL(*this, create_##sai_object_type##_entry)                                                                    \
                .WillByDefault(                                                                                                 \
                    [this](CREATE_PARAMS(sai_object_type)) {                                                                    \
                        return old_sai_##sai_object_type##_api->create_##sai_object_type##_entry(CREATE_ARGS(sai_object_type)); \
                    });                                                                                                         \
            ON_CALL(*this, remove_##sai_object_type##_entry)                                                                    \
                .WillByDefault(                                                                                                 \
                    [this](REMOVE_PARAMS(sai_object_type)) {                                                                    \
                        return old_sai_##sai_object_type##_api->remove_##sai_object_type##_entry(REMOVE_ARGS(sai_object_type)); \
                    });                                                                                                         \
        }                                                                                                                       \
        MOCK_METHOD(sai_status_t, create_##sai_object_type##_entry, (CREATE_PARAMS(sai_object_type)));                          \
        MOCK_METHOD(sai_status_t, remove_##sai_object_type##_entry, (REMOVE_PARAMS(sai_object_type)));                          \
    };                                                                                                                          \
    mock_sai_##sai_object_type##_api_t *mock_sai_##sai_object_type##_api;                                                       \
    sai_status_t mock_create_##sai_object_type##_entry(CREATE_PARAMS(sai_object_type))                                          \
    {                                                                                                                           \
        return mock_sai_##sai_object_type##_api->create_##sai_object_type##_entry(CREATE_ARGS(sai_object_type));                \
    }                                                                                                                           \
    sai_status_t mock_remove_##sai_object_type##_entry(REMOVE_PARAMS(sai_object_type))                                          \
    {                                                                                                                           \
        return mock_sai_##sai_object_type##_api->remove_##sai_object_type##_entry(REMOVE_ARGS(sai_object_type));                \
    }                                                                                                                           \
    void apply_sai_##sai_object_type##_api_mock()                                                                               \
    {                                                                                                                           \
        mock_sai_##sai_object_type##_api = new NiceMock<mock_sai_##sai_object_type##_api_t>();                                  \
                                                                                                                                \
        old_sai_##sai_object_type##_api = sai_##sai_object_type##_api;                                                          \
        ut_sai_##sai_object_type##_api = *sai_##sai_object_type##_api;                                                          \
        sai_##sai_object_type##_api = &ut_sai_##sai_object_type##_api;                                                          \
                                                                                                                                \
        sai_##sai_object_type##_api->create_##sai_object_type##_entry = mock_create_##sai_object_type##_entry;                  \
        sai_##sai_object_type##_api->remove_##sai_object_type##_entry = mock_remove_##sai_object_type##_entry;                  \
    }                                                                                                                           \
    void remove_sai_##sai_object_type##_api_mock()                                                                              \
    {                                                                                                                           \
        sai_##sai_object_type##_api = old_sai_##sai_object_type##_api;                                                          \
        delete mock_sai_##sai_object_type##_api;                                                                                \
    }

#define DEFINE_SAI_GENERIC_API_MOCK(sai_api_name, sai_object_type)                                                           \
    sai_##sai_api_name##_api_t *old_sai_##sai_api_name##_api;                                                                \
    sai_##sai_api_name##_api_t ut_sai_##sai_api_name##_api;                                                                  \
    class mock_sai_##sai_api_name##_api_t                                                                                    \
    {                                                                                                                        \
    public:                                                                                                                  \
        mock_sai_##sai_api_name##_api_t()                                                                                    \
        {                                                                                                                    \
            ON_CALL(*this, create_##sai_object_type)                                                                         \
                .WillByDefault(                                                                                              \
                    [this](GENERIC_CREATE_PARAMS(sai_object_type)) {                                                         \
                        return old_sai_##sai_api_name##_api->create_##sai_object_type(GENERIC_CREATE_ARGS(sai_object_type)); \
                    });                                                                                                      \
            ON_CALL(*this, remove_##sai_object_type)                                                                         \
                .WillByDefault(                                                                                              \
                    [this](GENERIC_REMOVE_PARAMS(sai_object_type)) {                                                         \
                        return old_sai_##sai_api_name##_api->remove_##sai_object_type(GENERIC_REMOVE_ARGS(sai_object_type)); \
                    });                                                                                                      \
        }                                                                                                                    \
        MOCK_METHOD(sai_status_t, create_##sai_object_type, (GENERIC_CREATE_PARAMS(sai_object_type)));                       \
        MOCK_METHOD(sai_status_t, remove_##sai_object_type, (GENERIC_REMOVE_PARAMS(sai_object_type)));                       \
    };                                                                                                                       \
    mock_sai_##sai_api_name##_api_t *mock_sai_##sai_api_name##_api;                                                          \
    sai_status_t mock_create_##sai_object_type(GENERIC_CREATE_PARAMS(sai_object_type))                                       \
    {                                                                                                                        \
        return mock_sai_##sai_api_name##_api->create_##sai_object_type(GENERIC_CREATE_ARGS(sai_object_type));                \
    }                                                                                                                        \
    sai_status_t mock_remove_##sai_object_type(GENERIC_REMOVE_PARAMS(sai_object_type))                                       \
    {                                                                                                                        \
        return mock_sai_##sai_api_name##_api->remove_##sai_object_type(GENERIC_REMOVE_ARGS(sai_object_type));                \
    }                                                                                                                        \
    void apply_sai_##sai_api_name##_api_mock()                                                                               \
    {                                                                                                                        \
        mock_sai_##sai_api_name##_api = new NiceMock<mock_sai_##sai_api_name##_api_t>();                                     \
                                                                                                                             \
        old_sai_##sai_api_name##_api = sai_##sai_api_name##_api;                                                             \
        ut_sai_##sai_api_name##_api = *sai_##sai_api_name##_api;                                                             \
        sai_##sai_api_name##_api = &ut_sai_##sai_api_name##_api;                                                             \
                                                                                                                             \
        sai_##sai_api_name##_api->create_##sai_object_type = mock_create_##sai_object_type;                                  \
        sai_##sai_api_name##_api->remove_##sai_object_type = mock_remove_##sai_object_type;                                  \
    }                                                                                                                        \
    void remove_sai_##sai_api_name##_api_mock()                                                                              \
    {                                                                                                                        \
        sai_##sai_api_name##_api = old_sai_##sai_api_name##_api;                                                             \
        delete mock_sai_##sai_api_name##_api;                                                                                \
    }

// Stores pointers to mock apply/remove functions to avoid needing to manually call each function
#define INIT_SAI_API_MOCK(sai_object_type)                          \
    apply_mock_fns.insert(&apply_sai_##sai_object_type##_api_mock); \
    remove_mock_fns.insert(&remove_sai_##sai_object_type##_api_mock);

void MockSaiApis()
{
    if (apply_mock_fns.empty())
    {
        EXPECT_TRUE(false) << "No mock application functions found. Did you call DEFINE_SAI_API_MOCK and INIT_SAI_API_MOCK for the necessary SAI object type?";
    }

    for (auto apply_fn : apply_mock_fns)
    {
        (*apply_fn)();
    }
}

void RestoreSaiApis()
{
    for (auto remove_fn : remove_mock_fns)
    {
        (*remove_fn)();
    }
}
