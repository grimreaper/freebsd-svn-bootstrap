
/******************************************************************************
 *
 * Module Name: amutils - interpreter/scanner utilities
 *              $Revision: 66 $
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999, 2000, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights.  You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code.  No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision.  In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change.  Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee.  Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution.  In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE.  ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT,  ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES.  INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS.  INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES.  THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government.  In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************/

#define __AMUTILS_C__

#include "acpi.h"
#include "acparser.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          INTERPRETER
        MODULE_NAME         ("amutils")


typedef struct Internal_Search_st
{
    ACPI_OPERAND_OBJECT         *DestObj;
    UINT32                      Index;
    ACPI_OPERAND_OBJECT         *SourceObj;

} INTERNAL_PKG_SEARCH_INFO;


/* Used to traverse nested packages when copying*/

INTERNAL_PKG_SEARCH_INFO        CopyLevel[MAX_PACKAGE_DEPTH];


static NATIVE_CHAR          hex[] =
    {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlEnterInterpreter
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Enter the interpreter execution region
 *
 ******************************************************************************/

void
AcpiAmlEnterInterpreter (void)
{
    FUNCTION_TRACE ("AmlEnterInterpreter");


    AcpiCmAcquireMutex (ACPI_MTX_EXECUTE);

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlExitInterpreter
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Exit the interpreter execution region
 *
 * Cases where the interpreter is unlocked:
 *      1) Completion of the execution of a control method
 *      2) Method blocked on a Sleep() AML opcode
 *      3) Method blocked on an Acquire() AML opcode
 *      4) Method blocked on a Wait() AML opcode
 *      5) Method blocked to acquire the global lock
 *      6) Method blocked to execute a serialized control method that is
 *          already executing
 *      7) About to invoke a user-installed opregion handler
 *
 ******************************************************************************/

void
AcpiAmlExitInterpreter (void)
{
    FUNCTION_TRACE ("AmlExitInterpreter");


    AcpiCmReleaseMutex (ACPI_MTX_EXECUTE);

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlValidateObjectType
 *
 * PARAMETERS:  Type            Object type to validate
 *
 * DESCRIPTION: Determine if a type is a valid ACPI object type
 *
 ******************************************************************************/

BOOLEAN
AcpiAmlValidateObjectType (
    ACPI_OBJECT_TYPE        Type)
{

    if ((Type > ACPI_TYPE_MAX && Type < INTERNAL_TYPE_BEGIN) ||
        (Type > INTERNAL_TYPE_MAX))
    {
        return (FALSE);
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlTruncateFor32bitTable
 *
 * PARAMETERS:  ObjDesc         - Object to be truncated
 *              WalkState       - Current walk state
 *                                (A method must be executing)
 *
 * RETURN:      none
 *
 * DESCRIPTION: Truncate a number to 32-bits if the currently executing method
 *              belongs to a 32-bit ACPI table.
 *
 ******************************************************************************/

void
AcpiAmlTruncateFor32bitTable (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState)
{

    /*
     * Object must be a valid number and we must be executing
     * a control method
     */

    if ((!ObjDesc) ||
        (ObjDesc->Common.Type != ACPI_TYPE_NUMBER) ||
        (!WalkState->MethodNode))
    {
        return;
    }

    if (WalkState->MethodNode->Flags & ANOBJ_DATA_WIDTH_32)
    {
        /*
         * We are running a method that exists in a 32-bit ACPI table.
         * Truncate the value to 32 bits by zeroing out the upper 32-bit field
         */
        ObjDesc->Number.Value &= (ACPI_INTEGER) ACPI_UINT32_MAX;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlAcquireGlobalLock
 *
 * PARAMETERS:  Rule            - Lock rule: AlwaysLock, NeverLock
 *
 * RETURN:      TRUE/FALSE indicating whether the lock was actually acquired
 *
 * DESCRIPTION: Obtain the global lock and keep track of this fact via two
 *              methods.  A global variable keeps the state of the lock, and
 *              the state is returned to the caller.
 *
 ******************************************************************************/

BOOLEAN
AcpiAmlAcquireGlobalLock (
    UINT32                  Rule)
{
    BOOLEAN                 Locked = FALSE;
    ACPI_STATUS             Status;


    FUNCTION_TRACE ("AmlAcquireGlobalLock");


    /*  Only attempt lock if the Rule says so */

    if (Rule == (UINT32) GLOCK_ALWAYS_LOCK)
    {
        /*  OK to get the lock   */

        Status = AcpiEvAcquireGlobalLock ();
        if (ACPI_FAILURE (Status))
        {
            DEBUG_PRINT (ACPI_ERROR, ("Get Global Lock Failed!!\n"));
        }

        if (ACPI_SUCCESS (Status))
        {
            AcpiGbl_GlobalLockSet = TRUE;
            Locked = TRUE;
        }
    }

    return_VALUE (Locked);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlReleaseGlobalLock
 *
 * PARAMETERS:  LockedByMe      - Return value from corresponding call to
 *                                AcquireGlobalLock.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release the global lock if it is locked.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiAmlReleaseGlobalLock (
    BOOLEAN                 LockedByMe)
{

    FUNCTION_TRACE ("AmlReleaseGlobalLock");


    /* Only attempt unlock if the caller locked it */

    if (LockedByMe)
    {
        /* Double check against the global flag */

        if (AcpiGbl_GlobalLockSet)
        {
            /* OK, now release the lock */

            AcpiEvReleaseGlobalLock ();
            AcpiGbl_GlobalLockSet = FALSE;
        }

        else
        {
            DEBUG_PRINT (ACPI_ERROR, ("Global lock was not set\n"));
        }
    }


    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlDigitsNeeded
 *
 * PARAMETERS:  val             - Value to be represented
 *              base            - Base of representation
 *
 * RETURN:      the number of digits needed to represent val in base
 *
 ******************************************************************************/

UINT32
AcpiAmlDigitsNeeded (
    ACPI_INTEGER            val,
    UINT32                  base)
{
    UINT32                  NumDigits = 0;


    FUNCTION_TRACE ("AmlDigitsNeeded");


    if (base < 1)
    {
        REPORT_ERROR (("AmlDigitsNeeded: Internal error - Invalid base\n"));
    }

    else
    {
        for (NumDigits = 1 + (val < 0); (val = ACPI_DIVIDE (val,base)); ++NumDigits)
        { ; }
    }

    return_VALUE (NumDigits);
}


/*******************************************************************************
 *
 * FUNCTION:    ntohl
 *
 * PARAMETERS:  Value           - Value to be converted
 *
 * DESCRIPTION: Convert a 32-bit value to big-endian (swap the bytes)
 *
 ******************************************************************************/

static UINT32
_ntohl (
    UINT32                  Value)
{
    union
    {
        UINT32              Value;
        UINT8               Bytes[4];
    } Out;

    union
    {
        UINT32              Value;
        UINT8               Bytes[4];
    } In;


    In.Value = Value;

    Out.Bytes[0] = In.Bytes[3];
    Out.Bytes[1] = In.Bytes[2];
    Out.Bytes[2] = In.Bytes[1];
    Out.Bytes[3] = In.Bytes[0];

    return (Out.Value);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlEisaIdToString
 *
 * PARAMETERS:  NumericId       - EISA ID to be converted
 *              OutString       - Where to put the converted string (8 bytes)
 *
 * DESCRIPTION: Convert a numeric EISA ID to string representation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiAmlEisaIdToString (
    UINT32                  NumericId,
    NATIVE_CHAR             *OutString)
{
    UINT32                  id;

    /* swap to big-endian to get contiguous bits */

    id = _ntohl (NumericId);

    OutString[0] = (char) ('@' + ((id >> 26) & 0x1f));
    OutString[1] = (char) ('@' + ((id >> 21) & 0x1f));
    OutString[2] = (char) ('@' + ((id >> 16) & 0x1f));
    OutString[3] = hex[(id >> 12) & 0xf];
    OutString[4] = hex[(id >> 8) & 0xf];
    OutString[5] = hex[(id >> 4) & 0xf];
    OutString[6] = hex[id & 0xf];
    OutString[7] = 0;

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlUnsignedIntegerToString
 *
 * PARAMETERS:  Value           - Value to be converted
 *              OutString       - Where to put the converted string (8 bytes)
 *
 * RETURN:      Convert a number to string representation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiAmlUnsignedIntegerToString (
    ACPI_INTEGER            Value,
    NATIVE_CHAR             *OutString)
{
    UINT32                  Count;
    UINT32                  DigitsNeeded;


    DigitsNeeded = AcpiAmlDigitsNeeded (Value, 10);

    OutString[DigitsNeeded] = '\0';

    for (Count = DigitsNeeded; Count > 0; Count--)
    {
        OutString[Count-1] = (NATIVE_CHAR) ('0' + (ACPI_MODULO (Value, 10)));
        Value = ACPI_DIVIDE (Value, 10);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiAmlBuildCopyInternalPackageObject
 *
 * PARAMETERS:  *SourceObj      - Pointer to the source package object
 *              *DestObj        - Where the internal object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to copy an internal package object
 *              into another internal package object.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiAmlBuildCopyInternalPackageObject (
    ACPI_OPERAND_OBJECT     *SourceObj,
    ACPI_OPERAND_OBJECT     *DestObj,
    ACPI_WALK_STATE         *WalkState)
{
    UINT32                      CurrentDepth = 0;
    ACPI_STATUS                 Status = AE_OK;
    UINT32                      Length = 0;
    UINT32                      ThisIndex;
    UINT32                      ObjectSpace = 0;
    ACPI_OPERAND_OBJECT         *ThisDestObj;
    ACPI_OPERAND_OBJECT         *ThisSourceObj;
    INTERNAL_PKG_SEARCH_INFO    *LevelPtr;


    FUNCTION_TRACE ("AmlBuildCopyInternalPackageObject");


    /*
     * Initialize the working variables
     */

    MEMSET ((void *) CopyLevel, 0, sizeof(CopyLevel));

    CopyLevel[0].DestObj    = DestObj;
    CopyLevel[0].SourceObj  = SourceObj;
    LevelPtr                = &CopyLevel[0];
    CurrentDepth            = 0;

    DestObj->Common.Type    = SourceObj->Common.Type;
    DestObj->Package.Count  = SourceObj->Package.Count;


    /*
     * Build an array of ACPI_OBJECTS in the buffer
     * and move the free space past it
     */

    DestObj->Package.Elements   = AcpiCmCallocate (
                                        (DestObj->Package.Count + 1) *
                                        sizeof (void *));
    if (!DestObj->Package.Elements)
    {
        /* Package vector allocation failure   */

        REPORT_ERROR (("AmlBuildCopyInternalPackageObject: Package vector allocation failure\n"));
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    DestObj->Package.NextElement = DestObj->Package.Elements;


    while (1)
    {
        ThisIndex       = LevelPtr->Index;
        ThisDestObj     = (ACPI_OPERAND_OBJECT *) LevelPtr->DestObj->Package.Elements[ThisIndex];
        ThisSourceObj   = (ACPI_OPERAND_OBJECT *) LevelPtr->SourceObj->Package.Elements[ThisIndex];

        if (IS_THIS_OBJECT_TYPE (ThisSourceObj, ACPI_TYPE_PACKAGE))
        {
            /*
             * If this object is a package then we go one deeper
             */
            if (CurrentDepth >= MAX_PACKAGE_DEPTH-1)
            {
                /*
                 * Too many nested levels of packages for us to handle
                 */
                DEBUG_PRINT (ACPI_ERROR,
                    ("AmlBuildCopyInternalPackageObject: Pkg nested too deep (max %X)\n",
                    MAX_PACKAGE_DEPTH));
                return_ACPI_STATUS (AE_LIMIT);
            }

            /*
             * Build the package object
             */
            ThisDestObj = AcpiCmCreateInternalObject (ACPI_TYPE_PACKAGE);
            LevelPtr->DestObj->Package.Elements[ThisIndex] = ThisDestObj;


            ThisDestObj->Common.Type        = ACPI_TYPE_PACKAGE;
            ThisDestObj->Package.Count      = ThisDestObj->Package.Count;

            /*
             * Save space for the array of objects (Package elements)
             * update the buffer length counter
             */
            ObjectSpace             = ThisDestObj->Package.Count *
                                        sizeof (ACPI_OPERAND_OBJECT);
            Length                  += ObjectSpace;
            CurrentDepth++;
            LevelPtr                = &CopyLevel[CurrentDepth];
            LevelPtr->DestObj       = ThisDestObj;
            LevelPtr->SourceObj     = ThisSourceObj;
            LevelPtr->Index         = 0;

        }   /* if object is a package */

        else
        {

            ThisDestObj = AcpiCmCreateInternalObject (
                                ThisSourceObj->Common.Type);
            LevelPtr->DestObj->Package.Elements[ThisIndex] = ThisDestObj;

            Status = AcpiAmlStoreObjectToObject(ThisSourceObj, ThisDestObj, WalkState);

            if (ACPI_FAILURE (Status))
            {
                /*
                 * Failure get out
                 */
                return_ACPI_STATUS (Status);
            }

            Length      +=ObjectSpace;

            LevelPtr->Index++;
            while (LevelPtr->Index >= LevelPtr->DestObj->Package.Count)
            {
                /*
                 * We've handled all of the objects at this level,  This means
                 * that we have just completed a package.  That package may
                 * have contained one or more packages itself
                 */
                if (CurrentDepth == 0)
                {
                    /*
                     * We have handled all of the objects in the top level
                     * package just add the length of the package objects
                     * and exit
                     */
                    return_ACPI_STATUS (AE_OK);
                }

                /*
                 * Go back up a level and move the index past the just
                 * completed package object.
                 */
                CurrentDepth--;
                LevelPtr = &CopyLevel[CurrentDepth];
                LevelPtr->Index++;
            }
        }   /* else object is NOT a package */
    }   /* while (1)  */
}


