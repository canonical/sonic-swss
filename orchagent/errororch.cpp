/*
 * Copyright 2019 Broadcom.  The term Broadcom refers to Broadcom Inc. and/or
 * its subsidiaries.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <vector>

#include "logger.h"
#include "tokenize.h"
#include "errororch.h"
#include "errormap.h"
#include "notifier.h"
#include "sai_serialize.h"
#include "swss/json.hpp"
using json = nlohmann::json;

#define EHF_OPERATION_SAIAPI_STATUS "saiapi_status"

/* Error handling framework currently handling only the following
 * tables and can be extended to other tables */
static std::map<std::string, std::string> m_ObjTableMap = {
    {"SAI_OBJECT_TYPE_ROUTE_ENTRY",       APP_ROUTE_TABLE_NAME},
    {"SAI_OBJECT_TYPE_NEIGHBOR_ENTRY",    APP_NEIGH_TABLE_NAME},
};

ErrorOrch::ErrorOrch(swss::DBConnector *asicDb, swss::DBConnector *errorDb, std::vector<std::string> &tableNames) :
    Orch(asicDb, tableNames)
{
    SWSS_LOG_ENTER();

    m_errorDb = std::shared_ptr<swss::DBConnector>(errorDb);
    /* Create Table objects for the requested tables */
    for(auto it : tableNames)
    {
        createTableObject(it);
    }

    /* Clear ERROR DB entries request from user interface */
    m_errorFlushNotificationConsumer = std::unique_ptr<swss::NotificationConsumer>(new swss::NotificationConsumer(asicDb, "FLUSH_ERROR_DB"));
    auto errorNotifier = new Notifier(m_errorFlushNotificationConsumer.get(), this, "FLUSH_ERROR_DB");
    Orch::addExecutor(errorNotifier);

    /* Add SAI API status error notifications support from Syncd */
    m_errorNotificationConsumer = std::unique_ptr<swss::NotificationConsumer>(new swss::NotificationConsumer(asicDb, "ERROR_NOTIFICATIONS"));
    errorNotifier = new Notifier(m_errorNotificationConsumer.get(), this, "SYNCD_ERROR_NOTIFICATIONS");
    Orch::addExecutor(errorNotifier);

    /* Create notification channels through which errors are sent to
     * the applications via error listerner */
    std::shared_ptr<swss::NotificationProducer> errorNotifications;
    for(auto objTable : m_ObjTableMap)
    {
        std::string strChannel = getErrorListenerChannelName(objTable.second);
        errorNotifications = std::make_shared<swss::NotificationProducer>(errorDb, strChannel);
        m_TableChannel[objTable.second] = errorNotifications;
        SWSS_LOG_INFO("Notification channel %s is created for %s",
                strChannel.c_str(), objTable.second.c_str());
    }
    SWSS_LOG_INFO("Ready to receive status notifications");
}

void ErrorOrch::doTask(swss::NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    std::string op;
    std::string data;
    std::vector<swss::FieldValueTuple> values;

    consumer.pop(op, data, values);

    SWSS_LOG_DEBUG("Received operation: %s data : %s", op.c_str(), data.c_str());
    if(&consumer == m_errorNotificationConsumer.get() && op == EHF_OPERATION_SAIAPI_STATUS)
    {
        json j = json::parse(data);

        std::string asicKey = j["key"];
        /* Extract SAI object type */
        const std::string &str_object_type = asicKey.substr(0, asicKey.find(":"));

        sai_object_type_t object_type;
        sai_deserialize_object_type(str_object_type, object_type);

        /* Find out the table object based on the object type */
        if(m_ObjTableMap.find(str_object_type) == m_ObjTableMap.end())
        {
            SWSS_LOG_INFO("Unsupported SAI object type %s",
                    str_object_type.c_str());
            return;
        }
        std::string tableName = m_ObjTableMap[str_object_type];

        if(m_TableOrchMap.find(tableName) == m_TableOrchMap.end())
        {
            SWSS_LOG_INFO("No registrants for %s mapping", tableName.c_str());
            return;
        }

        Orch *orch;
        orch = m_TableOrchMap[tableName];
        if(orch == NULL)
        {
            SWSS_LOG_INFO("Invalid Orch agent mapper object for %s", tableName.c_str());
            return;
        }

        std::vector<swss::FieldValueTuple> asicValues;
        std::vector<swss::FieldValueTuple> appValues;
        for(json::iterator it = j.begin(); it != j.end(); ++it)
        {
            asicValues.emplace_back(it.key(), it.value());
        }

        if(orch->mapToErrorDbFormat(object_type, asicValues, appValues) == false)
        {
            SWSS_LOG_ERROR("Mapping for object type %s is failed", str_object_type.c_str());
            return;
        }

        SWSS_LOG_DEBUG("Field values for %s after mapping: ", str_object_type.c_str());
        for(size_t i = 0; i < appValues.size(); i++)
        {
            SWSS_LOG_DEBUG("%s -> %s", fvField(appValues[i]).c_str(), fvValue(appValues[i]).c_str());
        }

        /* Convert SAI error code to SWSS error code */
        std::string swssRCStr = swss::ErrorMap::getSwssRCStr(j["rc"]);
        appValues.emplace_back("rc", swssRCStr);

        /* SAI operation could be create/remove/set */
        appValues.emplace_back("operation", j["operation"]);

        /* Update error database if the notification is about failure */
        std::string errKeyVal;
        auto dbValues = appValues;
        extractEntry(dbValues, "key", errKeyVal);
        updateErrorDb(tableName, errKeyVal, dbValues);

        /* Send the notification to registered applications */
        if(applNotificationEnabled(object_type))
        {
            json js;
            for(const auto &v: appValues)
            {
                js[fvField(v)] = fvValue(v);
            }
            std::string s = js.dump();
            std::string strOp = getOperation(tableName);
            sendNotification(tableName, strOp, s);
        }
    }
    else if(&consumer == m_errorFlushNotificationConsumer.get())
    {
        if(op == "ALL" || op == "TABLE")
        {
            flushErrorDb(op, data);
        }
        else
        {
            SWSS_LOG_ERROR("Received unknown flush ERROR DB request");
            return;
        }
    }
}

int ErrorOrch::flushErrorDb(const std::string &op, const std::string &data)
{
    SWSS_LOG_ENTER();
    std::shared_ptr<swss::Table> table;
    std::vector<std::string> keys;
    SWSS_LOG_DEBUG("ERROR DB flush request received, op %s, data %s", op.c_str(), data.c_str());

    std::string errTableName = data;
    for(auto iter : m_TableNameObjMap)
    {
        if((op != "ALL") && (errTableName != iter.first))
        {
            SWSS_LOG_INFO("Skipping flushing of entries for %s", errTableName.c_str());
            continue;
        }
        table = iter.second;
        if(table == NULL)
        {
            SWSS_LOG_INFO("Invalid Table object found for %s", iter.first.c_str());
            continue;
        }

        table->getKeys(keys);
        for(auto& key : keys)
        {
            table->del(key);
        }
        SWSS_LOG_DEBUG("Flushed ERROR DB entries for %s", iter.first.c_str());
    }
    return 0;
}

/* To filter out error notifications to the application */
bool ErrorOrch::applNotificationEnabled(_In_ sai_object_type_t object_type)
{
    /* This is used to filter out notifications to the applications based on the object type.
     * The complete list of supported objects are mentioned in m_ObjTableMap */
    return true;
}
void ErrorOrch::sendNotification(
        _In_ std::string& tableName,
        _In_ std::string& op,
        _In_ std::string& data,
        _In_ std::vector<swss::FieldValueTuple> &entry)
{
    SWSS_LOG_ENTER();
    int64_t rv = 0;

    SWSS_LOG_INFO("%s %s", op.c_str(), data.c_str());

    auto tableChannel = m_TableChannel.find(tableName);
    if(tableChannel == m_TableChannel.end())
    {
        SWSS_LOG_ERROR("Failed to find the notification channel for %s", tableName.c_str());
        return;
    }
    if(tableChannel->second == NULL)
    {
        SWSS_LOG_ERROR("Invalid notification channel found for %s", tableName.c_str());
        return;
    }

    std::shared_ptr<swss::NotificationProducer> notifications = tableChannel->second;

    rv = notifications->send(op, data, entry);

    SWSS_LOG_DEBUG("notification send successful, subscribers count %ld", rv);
}

void ErrorOrch::sendNotification(
        _In_ std::string& tableName,
        _In_ std::string& op,
        _In_ std::string& data)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry;

    sendNotification(tableName, op, data, entry);
}

void ErrorOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();
}

std::string ErrorOrch::getOperation(std::string &tableName)
{
    std::string strOperation = "oper_";
    strOperation += tableName;

    return strOperation;
}

std::string ErrorOrch::getErrorListenerChannelName(std::string &appDbTableName)
{
    std::string errorChannel = "ERROR_";
    errorChannel += appDbTableName + "_CHANNEL";

    return errorChannel;
}

std::string ErrorOrch::getErrorTableName(std::string &appDbTableName)
{
    std::string errorTableName = "ERROR_";
    errorTableName += appDbTableName;

    return errorTableName;
}

std::string ErrorOrch::getAppTableName(std::string &errDbTableName)
{
    std::string appTableName = errDbTableName;
    appTableName.erase(0, strlen("ERROR_"));

    return appTableName;
}

bool ErrorOrch::mappingHandlerRegister(std::string tableName, Orch* orch)
{
    SWSS_LOG_ENTER();
    if(m_TableOrchMap.find(tableName) != m_TableOrchMap.end())
    {
        SWSS_LOG_ERROR("Mapper for %s already exists", tableName.c_str());
        return false;
    }
    m_TableOrchMap[tableName] = orch;

    SWSS_LOG_INFO("Mapper for %s is registered", tableName.c_str());
    return true;
}

bool ErrorOrch::mappingHandlerDeRegister(std::string tableName)
{
    SWSS_LOG_ENTER();

    if(m_TableOrchMap.find(tableName) == m_TableOrchMap.end())
    {
        SWSS_LOG_ERROR("De-registration with Error Handling Framework failed for %s",
                tableName.c_str());
        return false;
    }

    m_TableOrchMap.erase(m_ObjTableMap[tableName]);
    SWSS_LOG_INFO("De-registration with Error Handling Framework for %s is successful",
            tableName.c_str());

    return true;
}

bool ErrorOrch::createTableObject(std::string &errTableName)
{
    SWSS_LOG_ENTER();
    if(m_TableNameObjMap.find(errTableName) != m_TableNameObjMap.end())
    {
        SWSS_LOG_ERROR("Table object for %s already exists", errTableName.c_str());
        return false;
    }
    m_TableNameObjMap[errTableName] = std::shared_ptr<swss::Table>(new swss::Table(m_errorDb.get(), errTableName));

    SWSS_LOG_DEBUG("Created Table object for %s", errTableName.c_str());
    return true;
}

bool ErrorOrch::deleteTableObject(std::string &errTableName)
{
    SWSS_LOG_ENTER();

    if(m_TableNameObjMap.find(errTableName) == m_TableNameObjMap.end())
    {
        SWSS_LOG_ERROR("Failed to remove Table object for %s", errTableName.c_str());
        return false;
    }

    m_TableNameObjMap.erase(errTableName);
    SWSS_LOG_DEBUG("Removed Table object for %s", errTableName.c_str());

    return true;
}

/* Extract the requested entry and erase it from the input list */
void ErrorOrch::extractEntry(std::vector<swss::FieldValueTuple> &values,
        const std::string &field, std::string &value)
{
    auto iu = values.begin();
    while(iu != values.end())
    {
        if(fvField(*iu) == field)
        {
            value = fvValue(*iu);
            iu = values.erase(iu);
            break;
        }
        else
        {
            iu++;
        }
    }
    return;
}

void ErrorOrch::updateErrorDb(std::string &tableName, const std::string &key,
         std::vector<swss::FieldValueTuple> &values)
{
    SWSS_LOG_ENTER();
    std::string strOp, strRc;
    bool entryFound = false;
    bool removeEntry = false;
    bool updateEntry = false;

    /* Extract current return code and operation */
    for(size_t i = 0; i < values.size(); i++)
    {
        if(fvField(values[i]) == "operation")
        {
            strOp = fvValue(values[i]);
        }
        else if(fvField(values[i]) == "rc")
        {
            strRc = fvValue(values[i]);
        }
    }

    /* Get ERROR DB table object */
    std::shared_ptr<swss::Table> table;
    std::string errTableName = getErrorTableName(tableName);
    if(m_TableNameObjMap.find(errTableName) == m_TableNameObjMap.end())
    {
        SWSS_LOG_INFO("Failed to find Table object for %s", errTableName.c_str());
        return;
    }

    table = m_TableNameObjMap[errTableName];
    if(table == NULL)
    {
        SWSS_LOG_INFO("Invalid Table object found for %s", errTableName.c_str());
        return;
    }

    /* Check if the entry with the key is present in ERROR DB */
    std::vector<swss::FieldValueTuple> ovalues;
    if(table->get(key, ovalues))
    {
        entryFound = true;
    }

    if(strRc == "SWSS_RC_SUCCESS")
    {
        /* Remove the entry if present in error database */
        if(entryFound == true)
        {
            removeEntry = true;
        }
    }
    else
    {
        if(strOp == "create")
        {
            /* Add new entry into error database */
            updateEntry = true;
        }
        else if(strOp == "remove")
        {
            if(entryFound == true)
            {
                /* Remove operation has failed due to the previous
                 * create/update operation failure */
                removeEntry = true;
            }
            else
            {
                /* Add new entry into error database */
                updateEntry = true;
            }
        }
        else if(strOp == "set")
        {
            /* Add/Update entry in the error database */
            updateEntry = true;
        }
    }
    SWSS_LOG_DEBUG("key %s operation %s RC %s Entry found %d update %d remove %d ",
            key.c_str(), strOp.c_str(), strRc.c_str(),
            entryFound, updateEntry, removeEntry);
    if(updateEntry == true)
    {
        /* Create/Update the database entry */
        table->set(key, values);
        SWSS_LOG_INFO("Publish %s(ok) to error db", key.c_str());
    }
    else if(removeEntry == true)
    {
        /* Remove the entry from database */
        table->del(key);
        SWSS_LOG_INFO("Removed %s(ok) from error db", key.c_str());
    }

    return;
}

/* This is used by other Orch Agents to add the entry into error database
 * and optionally send a notification to the applications     */
void ErrorOrch::addErrorEntry(sai_object_type_t object_type,
        std::vector<swss::FieldValueTuple> &appValues, uint32_t flags)
{
    SWSS_LOG_ENTER();

    std::shared_ptr<swss::Table> table;

    /* Extract SAI object type */
    std::string str_object_type = sai_serialize_object_type(object_type);
    SWSS_LOG_DEBUG("Field values received for %s: ", str_object_type.c_str());
    for(size_t i = 0; i < appValues.size(); i++)
    {
        SWSS_LOG_DEBUG("%s -> %s", fvField(appValues[i]).c_str(),
                fvValue(appValues[i]).c_str());
    }

    /* Find out the table object based on the object type */
    if(m_ObjTableMap.find(str_object_type) == m_ObjTableMap.end())
    {
        SWSS_LOG_INFO("Unsupported SAI object type %s",
                str_object_type.c_str());
        return;
    }

    std::string tableName = m_ObjTableMap[str_object_type];
    if(m_TableOrchMap.find(tableName) == m_TableOrchMap.end())
    {
        SWSS_LOG_INFO("Error handling is not supported for %s",
                tableName.c_str());
        return;
    }

    /* Update error database if the notification is about failure */
    std::string errKeyVal;
    auto dbValues = appValues;
    extractEntry(dbValues, "key", errKeyVal);
    updateErrorDb(tableName, errKeyVal, dbValues);

    /* Send the notification to registered applications */
    if((flags & ERRORORCH_FLAGS_NOTIF_SEND) && (applNotificationEnabled(object_type)))
    {
        json js;
        for(const auto &v: appValues)
        {
            js[fvField(v)] = fvValue(v);
        }
        std::string s = js.dump();
        std::string strOp = getOperation(tableName);
        sendNotification(tableName, strOp, s);
    }
}
