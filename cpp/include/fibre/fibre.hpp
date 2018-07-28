#ifndef __FIBRE_HPP
#define __FIBRE_HPP

#include <stdint.h>
#include <stdlib.h>

// TODO: remove
#include <vector>
#include <unordered_map>

/**
 * @brief Don't launch any scheduler thread.
 * 
 * The user application must call fibre::schedule_all() periodically, otherwise
 * Fibre will not emit any data. This option is intended for systems that don't
 * support threading.
 */
#define SCHEDULER_MODE_MANUAL 1

/**
 * @brief Launch one global scheduler thread that will handle all remote nodes.
 * This is recommended for embedded systems that don't support dynamic memory.
 */
#define SCHEDULER_MODE_GLOBAL_THREAD 2

/**
 * @brief Launch one scheduler thread per remote node.
 * This is recommended for desktop class systems.
 */
#define SCHEDULER_MODE_PER_NODE_THREAD 3



// log topics
//struct CONFIG_LOG_INPUT {};
//struct CONFIG_LOG_OUTPUT {};
//struct CONFIG_LOG_GENERAL {};
//struct CONFIG_LOG_TCP {};
//struct CONFIG_LOG_USB {};

#define LOG_TOPICS(X) \
    X(GENERAL, "general") \
    X(INPUT, "input") \
    X(OUTPUT, "output") \
    X(SERDES, "serdes") \
    X(TCP, "tcp")

#include "logging.hpp"

#include "fibre_config.hpp"

//#ifdef DEBUG_FIBRE
//#define LOG_FIBRE(...)  do { printf("%s %s(): ", __FILE__, __func__); printf(__VA_ARGS__); printf("\r\n"); } while (0)
//#else
//#define LOG_FIBRE(...)  ((void) 0)
//#endif



// Default CRC-8 Polynomial: x^8 + x^5 + x^4 + x^2 + x + 1
// Can protect a 4 byte payload against toggling of up to 5 bits
//  source: https://users.ece.cmu.edu/~koopman/crc/index.html
constexpr uint8_t CANONICAL_CRC8_POLYNOMIAL = 0x37;
constexpr uint8_t CANONICAL_CRC8_INIT = 0x42;

constexpr size_t CRC8_BLOCKSIZE = 4;

// Default CRC-16 Polynomial: 0x9eb2 x^16 + x^13 + x^12 + x^11 + x^10 + x^8 + x^6 + x^5 + x^2 + 1
// Can protect a 135 byte payload against toggling of up to 5 bits
//  source: https://users.ece.cmu.edu/~koopman/crc/index.html
// Also known as CRC-16-DNP
constexpr uint16_t CANONICAL_CRC16_POLYNOMIAL = 0x3d65;
constexpr uint16_t CANONICAL_CRC16_INIT = 0x1337;

constexpr uint8_t CANONICAL_PREFIX = 0xAA;


/* Forward declarations ------------------------------------------------------*/

#include "uuid.hpp"
#include "threading_utils.hpp"

namespace fibre {
    struct global_state_t;

    class IncomingConnectionDecoder; // input.hpp
    class OutputPipe; // output.hpp
    class RemoteNode; // remote_node.hpp
    class LocalEndpoint; // local_function.hpp
}

#include "stream.hpp"
#include "decoders.hpp"
#include "input.hpp"
#include "output.hpp"
#include "remote_node.hpp"
#include "local_function.hpp"
#include "local_ref_type.hpp"
#include "types.hpp"

namespace fibre {
    struct global_state_t {
        bool initialized = false;
        Uuid own_uuid;
        std::unordered_map<Uuid, RemoteNode> remote_nodes_;
        std::vector<const fibre::LocalRefType*> ref_types_ = std::vector<const fibre::LocalRefType*>();
        std::vector<const fibre::LocalEndpoint*> functions_ = std::vector<const fibre::LocalEndpoint*>();
        std::thread scheduler_thread;
        AutoResetEvent output_pipe_ready;
        AutoResetEvent output_channel_ready;
    };

    extern global_state_t global_state;

    void init();
    void publish_function(LocalEndpoint* function);
/*
    // @brief Registers the specified application object list using the provided endpoint table.
    // This function should only be called once during the lifetime of the application. TODO: fix this.
    // @param application_objects The application objects to be registred.
    template<typename T>
    int publish_object(T& application_objects) {
    //    static constexpr size_t endpoint_list_size = 1 + T::endpoint_count;
    //    static Endpoint* endpoint_list[endpoint_list_size];
    //    static auto endpoint_provider = EndpointProvider_from_MemberList<T>(application_objects);
        using ref_type = ::FibreRefType<T>;
        ref_type& asd = fibre::global_instance_of<ref_type>();
        publish_ref_type(&asd);
        // TODO: publish object
        return 0;
    }
    void publish_ref_type(FibreRefType* type);
    */

    RemoteNode* get_remote_node(Uuid uuid);

#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_MANUAL
    void schedule_all();
#endif


    struct user_function_tag {};
    struct builtin_function_tag {};
} // namespace fibre




template<typename T, typename TAG>
class StaticLinkedListElement {
private:
    struct iterator : std::iterator<std::forward_iterator_tag, T> {
    public:
        explicit iterator(StaticLinkedListElement* head)
            : head_(head) {}
        T& operator*() const { return head_->val_; }
        iterator& operator++() { head_ = head_->next_; return *this; }
        bool operator==(iterator other) const { return head_ == other.head_; }
        bool operator!=(iterator other) const { return !(*this == other); }
    private:
        StaticLinkedListElement* head_;
    };
    struct list {
        static iterator begin() {
            return iterator(get_list_head());
        }
        static iterator end() {
            return iterator(nullptr);
        }
    };

public:
    StaticLinkedListElement(T val) : val_(val)
    {
        *get_list_tail() = this;
        get_list_tail() = &(this->next_);
    }

    static list get_list() {
        return list();
    }

private:
    StaticLinkedListElement* next_ = nullptr;
    T val_;

    static StaticLinkedListElement*& get_list_head() {
        // maybe declaring this variable inside a header file might lead to
        // multiple instances if the function is called in multiple compilation
        // units. TODO: verify if a single instance is guaranteed.
        static StaticLinkedListElement* list_head = nullptr;
        return list_head;
    }

    static StaticLinkedListElement**& get_list_tail() {
        static StaticLinkedListElement** list_tail = &get_list_head();
        return list_tail;
    }
};






/* Export macros -------------------------------------------------------------*/

#define FIBRE_EXPORT_FUNCTION_(tag, func_name, inputs, outputs) \
    constexpr auto func_name ## __function_properties = \
        fibre::make_function_props(#func_name) \
        inputs \
        outputs; \
    auto func_name ## __endpoint = fibre::make_local_function_endpoint< \
        decltype(func_name), func_name, \
        decltype(func_name ## __function_properties)>(func_name ## __function_properties); \
    StaticLinkedListElement<const fibre::LocalEndpoint*, tag> func_name ## __linked_list_element(&(func_name ## __endpoint))

#define FIBRE_EXPORT_FUNCTION(func_name, inputs, outputs) FIBRE_EXPORT_FUNCTION_(fibre::user_function_tag, func_name, inputs, outputs)
#define FIBRE_EXPORT_BUILTIN_FUNCTION(func_name, inputs, outputs) FIBRE_EXPORT_FUNCTION_(fibre::builtin_function_tag, func_name, inputs, outputs)

#define INPUTS(...)     .with_inputs(__VA_ARGS__)
#define OUTPUTS(...)    .with_outputs(__VA_ARGS__)


#define FIBRE_EXPORT_TYPE_(tag, type_name, ...) \
    constexpr auto type_name ## __type_properties = \
        fibre::make_ref_type_props(#type_name) \
        __VA_ARGS__; \
    auto type_name ## __fibre_type = fibre::make_local_ref_type< \
        type_name, \
        decltype(type_name ## __type_properties)>(type_name ## __type_properties); \
    StaticLinkedListElement<const fibre::LocalRefType*, tag> type_name ## __linked_list_element(&(type_name ## __fibre_type))

#define FIBRE_EXPORT_TYPE(type_name, ...) FIBRE_EXPORT_TYPE_(fibre::user_function_tag, type_name, __VA_ARGS__)
#define FIBRE_EXPORT_BUILTIN_TYPE(type_name, ...) FIBRE_EXPORT_TYPE_(fibre::builtin_function_tag, type_name, __VA_ARGS__)

#define FIBRE_PROPERTY(name)    .with_properties(#name)


#endif // __FIBRE_HPP
