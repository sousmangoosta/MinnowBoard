/** @file
  LED2 control implementation
  
  Copyright (c) 2013, Intel Corporation
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Uefi.h>

#include <Library/LedLib.h>
#include <Library/E6xxGpioLib.h>

#define LED_Controller      1
#define LED_Port            6

/**
  Turn on LED2

**/
VOID
EFIAPI
Led2On (
  VOID
  )
{
  E6xxGpioOutputSet ( LED_Controller, LED_Port );
}


/**
  Turn off LED2

**/
VOID
EFIAPI
Led2Off (
  VOID
  )
{
  E6xxGpioOutputClear ( LED_Controller, LED_Port );
}


/**
  Toggle the on/off state of LED2

**/
VOID
EFIAPI
Led2Toggle (
  VOID
  )
{
  UINTN LedState;

  E6xxGpioPinLevel ( LED_Controller,
                     LED_Port,
                     &LedState );
  ( 0 != LedState ) ? Led2Off ( ) : Led2On ( );
}
