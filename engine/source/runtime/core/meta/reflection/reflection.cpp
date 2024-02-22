#include "reflection.h"
#include <cstring>
#include <map>

namespace Piccolo
{
    namespace Reflection
    {
        const char* k_unknown_type = "UnknownType";
        const char* k_unknown      = "Unknown";

        static std::map<std::string, ClassFunctionTuple*>       m_class_map;
        static std::multimap<std::string, FieldFunctionTuple*>  m_field_map;
        static std::multimap<std::string, MethodFunctionTuple*> m_method_map;
        static std::map<std::string, ArrayFunctionTuple*>       m_array_map;

        void TypeMetaRegisterinterface::registerToFieldMap(const char* name, FieldFunctionTuple* value)
        {
            m_field_map.insert(std::make_pair(name, value));
        }
        void TypeMetaRegisterinterface::registerToMethodMap(const char* name, MethodFunctionTuple* value)
        {
            m_method_map.insert(std::make_pair(name, value));
        }
        void TypeMetaRegisterinterface::registerToArrayMap(const char* name, ArrayFunctionTuple* value)
        {
            if (m_array_map.find(name) == m_array_map.end())
            {
                m_array_map.insert(std::make_pair(name, value));
            }
            else
            {
                delete value;
            }
        }

        void TypeMetaRegisterinterface::registerToClassMap(const char* name, ClassFunctionTuple* value)
        {
            // 如果在m_class_map中没有找到注册的对应函数，则创建一个pair并插入map
            if (m_class_map.find(name) == m_class_map.end())
            {
                m_class_map.insert(std::make_pair(name, value));
            }
            else
            {
                delete value;
            }
        }

        void TypeMetaRegisterinterface::unregisterAll()
        {
            for (const auto& itr : m_field_map)
            {
                delete itr.second;
            }
            m_field_map.clear();
            for (const auto& itr : m_class_map)
            {
                delete itr.second;
            }
            m_class_map.clear();
            for (const auto& itr : m_array_map)
            {
                delete itr.second;
            }
            m_array_map.clear();
        }

        TypeMeta::TypeMeta(std::string type_name) : m_type_name(type_name)
        {
            m_is_valid = false;
            m_fields.clear();
            m_methods.clear();

            // equal_range()函数主要是求在multimap中有多少个重复的元素
            // equal_range返回值是两个迭Map类型的迭代器(返回pair)，分别指向该元素的起始位置和该元素结束位置的下一个元素的位置
            auto fields_iter = m_field_map.equal_range(type_name);
            while (fields_iter.first != fields_iter.second)
            {
                FieldAccessor f_field(fields_iter.first->second); // 调用Accessor的构造函数，且由于TypeMeta是FieldAccessor的友元类，所以可以直接调用private的含参构造函数
                m_fields.emplace_back(f_field); // 通过emplace_back来取消拷贝与析构f_field的这一步，直接原地构造后添加到vector
                m_is_valid = true;

                ++fields_iter.first;
            }

            auto methods_iter = m_method_map.equal_range(type_name);
            while (methods_iter.first != methods_iter.second)
            {
                MethodAccessor f_method(methods_iter.first->second);
                m_methods.emplace_back(f_method); 
                m_is_valid = true;

                ++methods_iter.first;
            }
        }

        TypeMeta::TypeMeta() : m_type_name(k_unknown_type), m_is_valid(false) { m_fields.clear(); m_methods.clear(); }

        TypeMeta TypeMeta::newMetaFromName(std::string type_name)
        {
            TypeMeta f_type(type_name);
            return f_type;
        }

        bool TypeMeta::newArrayAccessorFromName(std::string array_type_name, ArrayAccessor& accessor)
        {
            auto iter = m_array_map.find(array_type_name);

            if (iter != m_array_map.end())
            {
                ArrayAccessor new_accessor(iter->second);
                accessor = new_accessor;
                return true;
            }

            return false;
        }

        ReflectionInstance TypeMeta::newFromNameAndJson(std::string type_name, const Json& json_context)
        {
            auto iter = m_class_map.find(type_name);

            if (iter != m_class_map.end())
            {
                return ReflectionInstance(TypeMeta(type_name), (std::get<1>(*iter->second)(json_context)));
            }
            return ReflectionInstance();
        }

        Json TypeMeta::writeByName(std::string type_name, void* instance)
        {
            auto iter = m_class_map.find(type_name);

            if (iter != m_class_map.end())
            {
                return std::get<2>(*iter->second)(instance);
            }
            return Json();
        }

        std::string TypeMeta::getTypeName() { return m_type_name; }

        int TypeMeta::getFieldsList(FieldAccessor*& out_list)
        {
            int count = m_fields.size();
            out_list  = new FieldAccessor[count];
            for (int i = 0; i < count; ++i)
            {
                out_list[i] = m_fields[i];
            }
            return count;
        }

        int TypeMeta::getMethodsList(MethodAccessor*& out_list)
        {
            int count = m_methods.size();
            out_list  = new MethodAccessor[count];
            for (int i = 0; i < count; ++i)
            {
                out_list[i] = m_methods[i];
            }
            return count;
        }

        int TypeMeta::getBaseClassReflectionInstanceList(ReflectionInstance*& out_list, void* instance)
        {
            auto iter = m_class_map.find(m_type_name);

            if (iter != m_class_map.end())
            {
                return (std::get<0>(*iter->second))(out_list, instance);
            }

            return 0;
        }

        FieldAccessor TypeMeta::getFieldByName(const char* name)
        {
            const auto it = std::find_if(m_fields.begin(), m_fields.end(), [&](const auto& i) {
                return std::strcmp(i.getFieldName(), name) == 0;
            });
            if (it != m_fields.end())
                return *it;
            return FieldAccessor(nullptr);
        }

        MethodAccessor TypeMeta::getMethodByName(const char* name)
        {
            const auto it = std::find_if(m_methods.begin(), m_methods.end(), [&](const auto& i) {
                return std::strcmp(i.getMethodName(), name) == 0;
            });
            if (it != m_methods.end())
                return *it;
            return MethodAccessor(nullptr);
        }

        TypeMeta& TypeMeta::operator=(const TypeMeta& dest)
        {
            if (this == &dest)
            {
                return *this;
            }
            m_fields.clear();
            m_fields = dest.m_fields;

            
            m_methods.clear();
            m_methods = dest.m_methods;

            m_type_name = dest.m_type_name;
            m_is_valid  = dest.m_is_valid;

            return *this;
        }
        FieldAccessor::FieldAccessor()
        {
            m_field_type_name = k_unknown_type;
            m_field_name      = k_unknown;
            m_functions       = nullptr;
        }

        FieldAccessor::FieldAccessor(FieldFunctionTuple* functions) : m_functions(functions)
        {
            m_field_type_name = k_unknown_type;
            m_field_name      = k_unknown;
            if (m_functions == nullptr)
            {
                return;
            }

            m_field_name      = (std::get<3>(*m_functions))(); // 从生成的reflection文件中获取成员变量名称
            m_field_type_name = (std::get<4>(*m_functions))(); // 获取成员变量类型
        }

        // set和get函数
        void* FieldAccessor::get(void* instance)
        {
            // todo: should check validation
            return static_cast<void*>((std::get<1>(*m_functions))(instance));
        }

        void FieldAccessor::set(void* instance, void* value)
        {
            // todo: should check validation
            (std::get<0>(*m_functions))(instance, value);
        }

        // 获取类名
        TypeMeta FieldAccessor::getOwnerTypeMeta()
        {
            // todo: should check validation
            TypeMeta f_type((std::get<2>(*m_functions))());
            return f_type;
        }

        bool FieldAccessor::getTypeMeta(TypeMeta& field_type)
        {
            TypeMeta f_type(m_field_type_name);
            field_type = f_type;
            return f_type.m_is_valid;
        }

        const char* FieldAccessor::getFieldName() const { return m_field_name; }
        const char* FieldAccessor::getFieldTypeName() { return m_field_type_name; }

        bool FieldAccessor::isArrayType()
        {
            // todo: should check validation
            return (std::get<5>(*m_functions))();
        }

        FieldAccessor& FieldAccessor::operator=(const FieldAccessor& dest)
        {
            if (this == &dest)
            {
                return *this;
            }
            m_functions       = dest.m_functions;
            m_field_name      = dest.m_field_name;
            m_field_type_name = dest.m_field_type_name;
            return *this;
        }

        MethodAccessor::MethodAccessor()
        {
            m_method_name = k_unknown;
            m_functions   = nullptr;
        }

        MethodAccessor::MethodAccessor(MethodFunctionTuple* functions) : m_functions(functions)
        {
            m_method_name      = k_unknown;
            if (m_functions == nullptr)
            {
                return;
            }

            m_method_name      = (std::get<0>(*m_functions))();
        }
        const char* MethodAccessor::getMethodName() const{
            return (std::get<0>(*m_functions))();
        }
        MethodAccessor& MethodAccessor::operator=(const MethodAccessor& dest)
        {
            if (this == &dest)
            {
                return *this;
            }
            m_functions       = dest.m_functions;
            m_method_name      = dest.m_method_name;
            return *this;
        }
        void MethodAccessor::invoke(void* instance) { (std::get<1>(*m_functions))(instance); }
        ArrayAccessor::ArrayAccessor() :
            m_func(nullptr), m_array_type_name("UnKnownType"), m_element_type_name("UnKnownType")
        {}

        ArrayAccessor::ArrayAccessor(ArrayFunctionTuple* array_func) : m_func(array_func)
        {
            m_array_type_name   = k_unknown_type;
            m_element_type_name = k_unknown_type;
            if (m_func == nullptr)
            {
                return;
            }

            m_array_type_name   = std::get<3>(*m_func)();
            m_element_type_name = std::get<4>(*m_func)();
        }
        const char* ArrayAccessor::getArrayTypeName() { return m_array_type_name; }
        const char* ArrayAccessor::getElementTypeName() { return m_element_type_name; }
        void        ArrayAccessor::set(int index, void* instance, void* element_value)
        {
            // todo: should check validation
            size_t count = getSize(instance);
            // todo: should check validation(index < count)
            std::get<0> (*m_func)(index, instance, element_value);
        }

        void* ArrayAccessor::get(int index, void* instance)
        {
            // todo: should check validation
            size_t count = getSize(instance);
            // todo: should check validation(index < count)
            return std::get<1>(*m_func)(index, instance);
        }

        int ArrayAccessor::getSize(void* instance)
        {
            // todo: should check validation
            return std::get<2>(*m_func)(instance);
        }

        ArrayAccessor& ArrayAccessor::operator=(ArrayAccessor& dest)
        {
            if (this == &dest)
            {
                return *this;
            }
            m_func              = dest.m_func;
            m_array_type_name   = dest.m_array_type_name;
            m_element_type_name = dest.m_element_type_name;
            return *this;
        }

        ReflectionInstance& ReflectionInstance::operator=(ReflectionInstance& dest)
        {
            if (this == &dest)
            {
                return *this;
            }
            m_instance = dest.m_instance;
            m_meta     = dest.m_meta;

            return *this;
        }

        ReflectionInstance& ReflectionInstance::operator=(ReflectionInstance&& dest)
        {
            if (this == &dest)
            {
                return *this;
            }
            m_instance = dest.m_instance;
            m_meta     = dest.m_meta;

            return *this;
        }
    } // namespace Reflection
} // namespace Piccolo