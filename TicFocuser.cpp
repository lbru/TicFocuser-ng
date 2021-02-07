/*******************************************************************************
TicFocuser
Copyright (C) 2019-2021 Sebastian Baberowski

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*******************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <memory>
#include <string.h>
#include <iostream>

#include <fstream>
#include <string>

#include "TicFocuser_config.h"
#include "TicFocuser.h"

#include "connection/SerialConnection.h"
#include "connection/ticlib/TicDefs.h"

#ifdef WITH_LIBTIC
#include "connection/PololuUsbConnection.h"
#endif

#ifdef WITH_LIBUSB
#include "connection/LibUsbConnection.h"
#endif

#ifdef WITH_BLUETOOTH
#include "connection/BluetoothConnection.h"
#endif

//#include <indilogger.h>
//INDI::Logger::getInstance().print("TIC Focuser NG",INDI::Logger::DBG_WARNING, __FILE__, __LINE__,"jest context");


std::unique_ptr<TicFocuser> ticFocuser(new TicFocuser());

void ISGetProperties(const char *dev)
{
    ticFocuser->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int num)
{
    ticFocuser->ISNewSwitch(dev, name, states, names, num);
}

void ISNewText(    const char *dev, const char *name, char *texts[], char *names[], int num)
{
    ticFocuser->ISNewText(dev, name, texts, names, num);
}

void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int num)
{
    ticFocuser->ISNewNumber(dev, name, values, names, num);
}

void ISNewBLOB (const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
  INDI_UNUSED(dev);
  INDI_UNUSED(name);
  INDI_UNUSED(sizes);
  INDI_UNUSED(blobsizes);
  INDI_UNUSED(blobs);
  INDI_UNUSED(formats);
  INDI_UNUSED(names);
  INDI_UNUSED(n);
}

void ISSnoopDevice (XMLEle *root)
{
    ticFocuser->ISSnoopDevice(root);
}

TicFocuser::TicFocuser():
    lastTimerHitError(false),
    moveRelInitialValue(-1),
    lastFocusDir(FOCUS_INWARD)
{
    setVersion(TICFOCUSER_VERSION_MAJOR,TICFOCUSER_VERSION_MINOR);
    setSupportedConnections(CONNECTION_NONE);
    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_SYNC | FOCUSER_CAN_ABORT | FOCUSER_HAS_BACKLASH);

    InfoErrorS = new IText[tic_error_names_ui_size];
}

TicFocuser::~TicFocuser()
{
    delete [] InfoErrorS;
}

bool TicFocuser::initProperties()
{
    INDI::Focuser::initProperties();

    IUFillSwitch(&FocusParkingModeS[0],"FOCUS_PARKON","Enable",ISS_OFF);
    IUFillSwitch(&FocusParkingModeS[1],"FOCUS_PARKOFF","Disable",ISS_ON);
    IUFillSwitchVector(&FocusParkingModeSP,FocusParkingModeS,2,getDeviceName(),"FOCUS_PARK_MODE","Parking Mode",OPTIONS_TAB,IP_RW,ISR_1OFMANY,60,IPS_IDLE);

    IUFillSwitch(&IdlePowerS[0],"IDLE_POWER_ON", "On", ISS_ON);
    IUFillSwitch(&IdlePowerS[1],"IDLE_POWER_OFF", "Off", ISS_OFF);
    IUFillSwitchVector(&IdlePowerSP, IdlePowerS, 2, getDeviceName(), "IDLE_POWER", "Idle power", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    IUFillSwitch(&EnergizeFocuserS[0],"ENERGIZE_FOCUSER","Energize focuser",ISS_OFF);
    IUFillSwitch(&EnergizeFocuserS[1],"DEENERGIZE_FOCUSER","De-energize focuser",ISS_OFF);
    IUFillSwitchVector(&EnergizeFocuserSP,EnergizeFocuserS,2,getDeviceName(),"ENERGIZE_FOCUSER","Energize",MAIN_CONTROL_TAB,IP_RW,ISR_1OFMANY,60,IPS_IDLE);

    /***** INFO_TAB */
    IUFillText(&InfoS[VIN_VOLTAGE], "VIN_VOLTAGE", "Vin voltage", "");
    IUFillText(&InfoS[CURRENT_LIMIT], "CURRENT_LIMIT", "Current limit", "");
    IUFillText(&InfoS[STEP_MODE], "STEP_MODE", "Step mode", "");
    IUFillText(&InfoS[ENERGIZED], "ENERGIZED", "Energized", "");
    IUFillText(&InfoS[OPERATION_STATE], "OPERATION_STATE", "Operational state", "");
    IUFillTextVector(&InfoSP, InfoS, InfoTabSize, getDeviceName(), "TIC_INFO", "Tic Info", INFO_TAB, IP_RO, 60, IPS_IDLE);

    /***** INFO_TAB > Error */
    for (size_t i = 0; i < tic_error_names_ui_size; ++i)
        IUFillText(&InfoErrorS[i], tic_error_names_ui[i].name, tic_error_names_ui[i].name, "");
    IUFillTextVector(&InfoErrorSP, InfoErrorS, tic_error_names_ui_size, getDeviceName(), "TIC_INFO_ERROR", "Tic Error", INFO_TAB, IP_RO, 60, IPS_IDLE);

    /***** Connections */
#ifdef WITH_LIBTIC
    registerConnection(new PololuUsbConnection(this));
#endif
#ifdef WITH_LIBUSB
    registerConnection(new LibUsbConnection(this));
#endif
#ifdef WITH_BLUETOOTH
    registerConnection(new BluetoothConnection(this));
#endif
    registerConnection(new SerialConnection(this));

    return true;
}

bool TicFocuser::loadConfig(bool silent, const char *property)
{
    INDI::DefaultDevice::loadConfig(silent, property);

    // I cannot figure out for the life of me how INDI's loadConfig and ISNewSwitch
    // interact. The loaded config is not used when calling ISNewSwitch?

    idlePowerDown = IUFindOnSwitchIndex(&IdlePowerSP) == 1;

    if(idlePowerDown)
        deenergizeFocuser();

    return true;
}

bool TicFocuser::updateProperties()
{
    INDI::Focuser::updateProperties();

    if (isConnected())
    {
        defineSwitch(&IdlePowerSP);
        defineSwitch(&EnergizeFocuserSP);
        defineSwitch(&FocusParkingModeSP);
        defineText(&InfoSP);
        defineText(&InfoErrorSP);
    }
    else
    {
        deleteProperty(IdlePowerSP.name);
        deleteProperty(FocusParkingModeSP.name);
        deleteProperty(EnergizeFocuserSP.name);
        deleteProperty(InfoSP.name);
        deleteProperty(InfoErrorSP.name);
    }

    return true;
}

bool TicFocuser::ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n)
{
    return INDI::Focuser::ISNewNumber(dev,name,values,names,n);
}

bool TicFocuser::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    // first we check if it's for our device
    if (!strcmp(dev, getDeviceName()))
    {    
        // handle parking mode
        if(!strcmp(name, FocusParkingModeSP.name))
        {
            IUUpdateSwitch(&FocusParkingModeSP, states, names, n);
            FocusParkingModeSP.s = IPS_OK;
            IDSetSwitch(&FocusParkingModeSP, NULL);
            return true;
        }

        // handle idle power
        if(!strcmp(name, IdlePowerSP.name))
        {
            IUUpdateSwitch(&IdlePowerSP, states, names, n);
            auto newState = strcmp(names[0], IdlePowerS[0].name);
            IDSetSwitch(&IdlePowerSP, NULL);

            if(newState != idlePowerDown && FocusAbsPosNP.s == IPS_OK)
                newState ? deenergizeFocuser() : energizeFocuser();
                
            idlePowerDown = newState;

            IdlePowerSP.s = IPS_OK;

            return true;
        }

        // handle power state
        if(!strcmp(name, EnergizeFocuserSP.name))
        {
            bool res;

            if (!strcmp(names[0], EnergizeFocuserS[0].name))
                res = energizeFocuser();
            else
                res = deenergizeFocuser();

            EnergizeFocuserSP.s = res? IPS_OK: IPS_ALERT;
            IDSetSwitch(&EnergizeFocuserSP, NULL);

            return true;
        }


    }
    return INDI::Focuser::ISNewSwitch(dev,name,states,names,n);
}

bool TicFocuser::saveConfigItems(FILE *fp)
{
    if (!Focuser::saveConfigItems(fp))
        return false;

    IUSaveConfigSwitch(fp, &IdlePowerSP);
    IUSaveConfigSwitch(fp, &FocusParkingModeSP);

    return true;
}

bool TicFocuser::Disconnect() 
{
    // park focuser
    if (FocusParkingModeS[0].s != ISS_ON) {
        LOG_INFO("Parking mode disabled, parking not performed.");
        return Focuser::Disconnect();
    }
    else {
        FocusAbsPosNP.s = MoveAbsFocuser(0);
        
        while (FocusAbsPosNP.s != IPS_OK)
        {
            sleep(1);
            TimerHit();
        }
    }

    return Focuser::Disconnect();
}

bool TicFocuser::Connect() 
{
    return Focuser::Connect();
}

void TicFocuser::TimerHit() 
{
    if (!isConnected())
        return;

    TicConnectionInterface* conn = dynamic_cast<TicConnectionInterface*>(getActiveConnection());    
    TicDriverInterface& driverInterface = conn->getTicDriverInterface();

    TicDriverInterface::TicVariables ticVariables;
    bool res = driverInterface.getVariables(&ticVariables);

    if (res) {

        lastTimerHitError = false;

        FocusAbsPosN[0].value = ticVariables.currentPosition;
        FocusSyncN[0].value = ticVariables.currentPosition;

        if (FocusAbsPosNP.s == IPS_BUSY) {

            if (moveRelInitialValue >= 0) {
                FocusRelPosN[0].value = abs( moveRelInitialValue - ticVariables.currentPosition);
            }

            if ( ticVariables.currentPosition ==  ticVariables.targetPosition) {
                FocusAbsPosNP.s = IPS_OK;
                FocusRelPosNP.s = IPS_OK;
                moveRelInitialValue = -1;

                if(idlePowerDown)
                    deenergizeFocuser(false);
            }
        }

        IDSetNumber(&FocusAbsPosNP, nullptr);
        IDSetNumber(&FocusRelPosNP, nullptr);
        IDSetNumber(&FocusSyncNP, nullptr);

        /** INFO_TAB */
        char buf[20];
        std::snprintf(buf,sizeof(buf),"%.2f V", ((double)ticVariables.vinVoltage)/1000);
        IUSaveText( &InfoS[VIN_VOLTAGE], buf);
        if (ticVariables.currentLimit > 1000)
            std::snprintf(buf,sizeof(buf),"%.2f A", ((double)ticVariables.currentLimit)/1000);
        else
            std::snprintf(buf,sizeof(buf),"%d mA", ticVariables.currentLimit);

        IUSaveText( &InfoS[CURRENT_LIMIT], buf);
        IUSaveText( &InfoS[ENERGIZED], ticVariables.energized? "Yes": "No");
        IUSaveText( &InfoS[STEP_MODE], ticVariables.stepMode.c_str());
        IUSaveText( &InfoS[OPERATION_STATE], ticVariables.operationalState.c_str());
        IDSetText(&InfoSP, nullptr);

        /***** INFO_TAB > Error */
        for (size_t i = 0; i < tic_error_names_ui_size; ++i) 
        {
            if (tic_error_names_ui[i].code & ticVariables.errorStatus)
                IUSaveText(&InfoErrorS[i], "Error");
            else
                IUSaveText(&InfoErrorS[i], "-");
        }
        IDSetText(&InfoErrorSP, nullptr);

    }
    else if (!lastTimerHitError) {
        LOGF_ERROR("Cannot receive variables: %s", driverInterface.getLastErrorMsg());
        lastTimerHitError = true;
    }

    SetTimer(POLLMS);
}

bool TicFocuser::energizeFocuser(bool logMessage)
{
    TicConnectionInterface* conn = dynamic_cast<TicConnectionInterface*>(getActiveConnection());    
    TicDriverInterface& driverInterface = conn->getTicDriverInterface();

    if (!driverInterface.energize()) 
    {
        LOGF_ERROR("Cannot energize motor. Error: %s", driverInterface.getLastErrorMsg());
        return false;
    }
    else if (logMessage)
    {
        LOG_INFO("Focuser energized.");
    }

    if (!driverInterface.exitSafeStart())
    {
        LOGF_ERROR("Cannot exit safe start. Error: %s", driverInterface.getLastErrorMsg());
        return false;
    }

    return true;
}

bool TicFocuser::deenergizeFocuser(bool logMessage)
{
    TicConnectionInterface* conn = dynamic_cast<TicConnectionInterface*>(getActiveConnection());    
    TicDriverInterface& driverInterface = conn->getTicDriverInterface();

    if (!driverInterface.deenergize())
    {
        LOGF_ERROR("Cannot de-energize motor. Error: %s", driverInterface.getLastErrorMsg());
        return false;
    }
    else if (logMessage)
    {
        LOG_INFO("Focuser de-energized.");
    }

    return true;
}

bool TicFocuser::SyncFocuser(uint32_t ticks) 
{
    TicConnectionInterface* conn = dynamic_cast<TicConnectionInterface*>(getActiveConnection());    
    TicDriverInterface& driverInterface = conn->getTicDriverInterface();

    if (!driverInterface.haltAndSetPosition(ticks))
    {
        LOGF_ERROR("Cannot sync focuser. Error: %s", driverInterface.getLastErrorMsg());
        return false;
    }

    return true;
}

bool TicFocuser::AbortFocuser() 
{

    TicConnectionInterface* conn = dynamic_cast<TicConnectionInterface*>(getActiveConnection());    
    TicDriverInterface& driverInterface = conn->getTicDriverInterface();

    if (!driverInterface.haltAndHold())
    {
        LOGF_ERROR("Cannot abort TIC. Error: %s", driverInterface.getLastErrorMsg());
        return false;
    }

    return true;
}

IPState TicFocuser::MoveFocuser(FocusDirection dir, int speed, uint16_t duration)
{
    INDI_UNUSED(dir);
    INDI_UNUSED(speed);
    INDI_UNUSED(duration);
    LOG_ERROR("Focuser does not support timer based motion.");

    return IPS_ALERT;
}

IPState TicFocuser::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    int32_t absTicks = FocusAbsPosN[0].value;
    int32_t targetTicks;
    if (dir == FOCUS_OUTWARD)
        targetTicks = absTicks + ticks;
    else
        targetTicks = absTicks - ticks;

    IPState ret =  MoveAbsFocuser(targetTicks);

    moveRelInitialValue = ret == IPS_BUSY? absTicks: -1;

    FocusAbsPosNP.s = ret;
    IDSetNumber(&FocusAbsPosNP, nullptr);

    return ret;
}

IPState TicFocuser::MoveAbsFocuser(uint32_t ticks)
{ 
    if (ticks == FocusAbsPosN[0].value)
    {
        return IPS_OK;
    }
    else if (ticks > FocusAbsPosN[0].value)
    {
        if(lastFocusDir == FOCUS_INWARD && FocusBacklashS[INDI_ENABLED].s == ISS_ON)
        {
            uint32_t nominal = ticks;
            ticks = static_cast<uint32_t>(std::min(ticks + FocusBacklashN[0].value, FocusAbsPosN[0].max));
            LOGF_INFO("Apply backlash (in->out): +%d", ticks - nominal);
        }
        lastFocusDir = FOCUS_OUTWARD;
    }
    else if (ticks < FocusAbsPosN[0].value && FocusBacklashS[INDI_ENABLED].s == ISS_ON)
    {
        if(lastFocusDir == FOCUS_OUTWARD)
        {
            uint32_t nominal = ticks;
            ticks = static_cast<uint32_t>(std::max(ticks - FocusBacklashN[0].value, FocusAbsPosN[0].min));
            LOGF_INFO("Apply backlash (out->in): %d", ticks - nominal);
        }
        lastFocusDir = FOCUS_INWARD;
    }

    if (ticks < FocusAbsPosN[0].min || ticks > FocusAbsPosN[0].max)
    {
        LOGF_ERROR("Requested position is out of range: %d", ticks);
        return IPS_ALERT;
    }

    TicConnectionInterface* conn = dynamic_cast<TicConnectionInterface*>(getActiveConnection());    
    TicDriverInterface& driverInterface = conn->getTicDriverInterface();

    if(!driverInterface.isEnergized())
    {
        energizeFocuser(!idlePowerDown);
    }

    if (!driverInterface.setTargetPosition(ticks))
    {
        LOGF_ERROR("Cannot set target position. Error: %s", driverInterface.getLastErrorMsg());
        return IPS_ALERT;
    }
  
    return IPS_BUSY;
}

bool TicFocuser::SetFocuserBacklash(int32_t steps)
{
    INDI_UNUSED(steps);
    return true;
}
