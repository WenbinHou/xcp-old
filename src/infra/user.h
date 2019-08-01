#if !defined(_XCP_INFRA_USER_H_INCLUDED_)
#define _XCP_INFRA_USER_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    struct user_name_t
    {
        std::string user_name;
        std::string domain_user_name;
        std::string user_principal_name;  // Windows UPN
        std::string user_sid;

        XCP_DEFAULT_SERIALIZATION(user_name, domain_user_name, user_sid)

        std::string to_string() const {
            std::string result = "(";
            bool need_comma = false;
            if (!user_name.empty()) {
                result += "user_name=";
                result += user_name;
                need_comma = true;
            }
            if (!domain_user_name.empty()) {
                if (need_comma) result += ", ";
                result += "domain_user_name=";
                result += domain_user_name;
                need_comma = true;
            }
            if (!user_principal_name.empty()) {
                if (need_comma) result += ", ";
                result += "user_principal_name=";
                result += user_principal_name;
                need_comma = true;
            }
            if (!user_sid.empty()) {
                if (need_comma) result += ", ";
                result += "sid=";
                result += user_sid;
            }
            result += ")";
            return result;
        }
    };

    bool get_user_name(/*out*/ user_name_t& name);
    stdfs::path get_user_home_path(const user_name_t& name);

}  // namespace infra


#endif  // !defined(_XCP_INFRA_USER_H_INCLUDED_)
