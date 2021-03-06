#include <stdio.h>

#include "hal.h"

#include "ctrl.h"

#include "gps.h"
#include "timesync.h"
#include "format.h"

// ========================================================================================================================

void PrintTasks(void (*CONS_UART_Write)(char))
{ char Line[32];

  size_t FreeHeap = xPortGetFreeHeapSize();
  Format_String(CONS_UART_Write, "Task            Pr. Stack, ");
  Format_UnsDec(CONS_UART_Write, (uint32_t)FreeHeap, 4, 3);
  Format_String(CONS_UART_Write, "kB free\n");

  UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
  TaskStatus_t *pxTaskStatusArray = (TaskStatus_t *)pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
  if(pxTaskStatusArray==0) return;
  uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, NULL );
  for(UBaseType_t T=0; T<uxArraySize; T++)
  { TaskStatus_t *Task = pxTaskStatusArray+T;
    uint8_t Len=Format_String(Line, Task->pcTaskName);
    for( ; Len<=configMAX_TASK_NAME_LEN; )
      Line[Len++]=' ';
    Len+=Format_UnsDec(Line+Len, Task->uxCurrentPriority, 2); Line[Len++]=' ';
    // Line[Len++]='0'+Task->uxCurrentPriority; Line[Len++]=' ';
    Len+=Format_UnsDec(Line+Len, Task->usStackHighWaterMark, 3);
    Line[Len++]='\n'; Line[Len++]=0;
    Format_String(CONS_UART_Write, Line);
  }
  vPortFree( pxTaskStatusArray );
}

// ========================================================================================================================

#ifdef WITH_OLED
int OLED_DisplayStatus(uint32_t Time, uint8_t LineIdx=0)
{ char Line[20];
  Format_String(Line   , "OGN Tx/Rx      ");
  Format_HHMMSS(Line+10, Time);
  OLED_PutLine(LineIdx++, Line);
  Parameters.Print(Line);
  OLED_PutLine(LineIdx++, Line);
  return 0; }

int OLED_DisplayPosition(GPS_Position *GPS=0, uint8_t LineIdx=2)
{ char Line[20];
  if(GPS && GPS->isValid())
  { Line[0]=' ';
    Format_SignDec(Line+1,  GPS->Latitude /60, 6, 4); Line[9]=' ';
    Format_UnsDec (Line+10, GPS->Altitude /10, 5, 0); Line[15]='m';
    OLED_PutLine(LineIdx  , Line);
    Format_SignDec(Line,    GPS->Longitude/60, 7, 4);
    Format_SignDec(Line+10, GPS->ClimbRate,    4, 1);
    OLED_PutLine(LineIdx+1, Line);
    Format_UnsDec (Line   , GPS->Speed, 4, 1); Format_String(Line+5, "m/s  ");
    Format_UnsDec (Line+10, GPS->Heading, 4, 1); Line[15]='^';
    OLED_PutLine(LineIdx+2, Line);
    Format_String(Line, "0D/00sat DOP00.0");
    Line[0]+=GPS->FixMode; Format_UnsDec(Line+3, GPS->Satellites, 2);
    Format_UnsDec(Line+12, (uint16_t)GPS->HDOP, 3, 1);
    OLED_PutLine(LineIdx+3, Line);
  }
  else { OLED_PutLine(LineIdx, 0); OLED_PutLine(LineIdx+1, 0); OLED_PutLine(LineIdx+2, 0); OLED_PutLine(LineIdx+3, 0); }
  if(GPS && GPS->isDateValid())
  { Format_UnsDec (Line   , (uint16_t)GPS->Day,   2, 0); Line[2]='.';
    Format_UnsDec (Line+ 3, (uint16_t)GPS->Month, 2, 0); Line[5]='.';
    Format_UnsDec (Line+ 6, (uint16_t)GPS->Year , 2, 0); Line[8]=' '; Line[9]=' '; }
  else Format_String(Line, "          ");
  if(GPS && GPS->isTimeValid())
  { Format_UnsDec (Line+10, (uint16_t)GPS->Hour,  2, 0);
    Format_UnsDec (Line+12, (uint16_t)GPS->Min,   2, 0);
    Format_UnsDec (Line+14, (uint16_t)GPS->Sec,   2, 0);
  } else Line[10]=0;
  OLED_PutLine(LineIdx+4, Line);
  Line[0]=0;
  if(GPS && GPS->hasBaro)
  { Format_String(Line   , "0000.0hPa 00000m");
    Format_UnsDec(Line   , GPS->Pressure/4, 5, 1);
    Format_UnsDec(Line+10, GPS->StdAltitude/10, 5, 0); }
  OLED_PutLine(LineIdx+5, Line);
  return 0; }
#endif

// ========================================================================================================================

static NMEA_RxMsg NMEA;

static char Line[80];

static void PrintParameters(void)                               // print parameters stored in Flash
{ Parameters.Print(Line);
  xSemaphoreTake(CONS_Mutex, portMAX_DELAY);                   // ask exclusivity on UART1
  Format_String(CONS_UART_Write, Line);
  xSemaphoreGive(CONS_Mutex);                                  // give back UART1 to other tasks
}

#ifdef WITH_CONFIG
static void ReadParameters(void)  // read parameters requested by the user in the NMEA sent.
{ if((!NMEA.hasCheck()) || NMEA.isChecked() )
  { PrintParameters();
    if(NMEA.Parms==0)                                                      // if no parameter given
    { xSemaphoreTake(CONS_Mutex, portMAX_DELAY);                           // print a help message
      // Format_String(CONS_UART_Write, "$POGNS,<aircraft-type>,<addr-type>,<address>,<RFM69(H)W>,<Tx-power[dBm]>,<freq.corr.[kHz]>,<console baudrate[bps]>,<RF temp. corr.[degC]>,<pressure corr.[Pa]>\n");
      Format_String(CONS_UART_Write, "$POGNS[,<Name>=<Value>]\n");
      xSemaphoreGive(CONS_Mutex);                                          //
      return; }
    Parameters.ReadPOGNS(NMEA);
    PrintParameters();
    esp_err_t Err = Parameters.WriteToNVS();                                                  // erase and write the parameters into the Flash
    // if(Parameters.ReadFromNVS()!=ESP_OK) Parameters.setDefault();
    // Parameters.WriteToFlash();                                             // erase and write the parameters into the Flash
    // if(Parameters.ReadFromFlash()<0) Parameters.setDefault();              // read the parameters back: if invalid, set defaults
                                                                              // page erase lasts 20ms tus about 20 system ticks are lost here
  }
  // PrintParameters();
}
#endif

static void ProcessNMEA(void)     // process a valid NMEA that got to the console
{
#ifdef WITH_CONFIG
  if(NMEA.isPOGNS()) ReadParameters();
#endif
}

static void ProcessCtrlC(void)                                  // print system state to the console
{ xSemaphoreTake(CONS_Mutex, portMAX_DELAY);
  Parameters.Print(Line);
  Format_String(CONS_UART_Write, Line);
  Format_String(CONS_UART_Write, "GPS: ");
  Format_UnsDec(CONS_UART_Write, GPS_getBaudRate(), 1);
  Format_String(CONS_UART_Write, "bps");
  CONS_UART_Write(',');
  Format_UnsDec(CONS_UART_Write, GPS_PosPeriod, 3, 2);
  CONS_UART_Write('s');
  if(GPS_Status.PPS)         Format_String(CONS_UART_Write, ",PPS");
  if(GPS_Status.NMEA)        Format_String(CONS_UART_Write, ",NMEA");
  if(GPS_Status.UBX)         Format_String(CONS_UART_Write, ",UBX");
  if(GPS_Status.MAV)         Format_String(CONS_UART_Write, ",MAV");
  if(GPS_Status.BaudConfig)  Format_String(CONS_UART_Write, ",BaudOK");
  if(GPS_Status.ModeConfig)  Format_String(CONS_UART_Write, ",ModeOK");
  CONS_UART_Write('\r'); CONS_UART_Write('\n');
  PrintTasks(CONS_UART_Write);
  size_t Total, Used;
  if(SPIFFS_Info(Total, Used)==0)
  { Format_String(CONS_UART_Write, "SPIFFS: ");
    Format_UnsDec(CONS_UART_Write, Used/1024);
    Format_String(CONS_UART_Write, "kB used, ");
    Format_UnsDec(CONS_UART_Write, Total/1024);
    Format_String(CONS_UART_Write, "kB total\n"); }
  Parameters.WriteFile(stdout);
  xSemaphoreGive(CONS_Mutex); }

static void ProcessInput(void)
{ for( ; ; )
  { uint8_t Byte; int Err=CONS_UART_Read(Byte); if(Err<=0) break; // get byte from console, if none: exit the loop
    if(Byte==0x03) ProcessCtrlC();                            // if Ctrl-C received
    NMEA.ProcessByte(Byte);                                   // pass the byte through the NMEA processor
    if(NMEA.isComplete())                                     // if complete NMEA:
    { ProcessNMEA();                                          // interpret the NMEA
      NMEA.Clear(); }                                         // clear the NMEA processor for the next sentence
  }
}

// ========================================================================================================================

extern "C"
void vTaskCTRL(void* pvParameters)
{ uint32_t PrevTime=0;
  GPS_Position *PrevGPS=0;
  for( ; ; )
  { ProcessInput();
    vTaskDelay(5);
    LED_TimerCheck(5);
    uint32_t Time=TimeSync_Time();
    GPS_Position *GPS = GPS_getPosition();
    bool TimeChange = Time!=PrevTime;
    bool GPSchange  = GPS!=PrevGPS;
    if( (!TimeChange) && (!GPSchange) ) continue;
    PrevTime=Time; PrevGPS=GPS;
#ifdef WITH_OLED
    if(TimeChange) OLED_DisplayStatus(Time, 0);
    if(GPSchange)  OLED_DisplayPosition(GPS, 2);
#endif
#ifdef DEBUG_PRINT
    if(!TimeChange || (Time%60)!=0) continue;
    ProcessCtrlC();
#endif
  }
}

// ========================================================================================================================
