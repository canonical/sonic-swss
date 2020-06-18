/*
 * Copyright 2019 Broadcom Inc.
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

#if !defined(__PHY_PARSER_H__)
#define __PHY_PARSER_H__

#include "gearparserbase.h"

class PhyParser: public GearParserBase
{
public:
    bool parse();
    void setPhyId(int id) {m_phyId = id;};
    int getPhyId() {return m_phyId;};
private:
    int m_phyId;
};

#endif /* __PHY_PARSER_H__ */
