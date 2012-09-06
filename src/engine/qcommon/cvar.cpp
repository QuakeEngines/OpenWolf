/*
===========================================================================

OpenWolf GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the OpenWolf GPL Source Code (OpenWolf Source Code).  

OpenWolf Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

OpenWolf Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenWolf Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the OpenWolf Source Code is also subject to certain additional terms. 
You should have received a copy of these additional terms immediately following the 
terms and conditions of the GNU General Public License which accompanied the OpenWolf 
Source Code.  If not, please request a copy in writing from id Software at the address 
below.

If you have questions concerning this license or the applicable additional terms, you 
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, 
Maryland 20850 USA.

===========================================================================
*/

// cvar.c -- dynamic variable tracking

#include "../idLib/precompiled.h"
#include "../qcommon/q_shared.h"
#include "qcommon.h"

convar_t         *cvar_vars;
convar_t         *cvar_cheats;
int             cvar_modifiedFlags;

#define MAX_CVARS   2048
convar_t          cvar_indexes[MAX_CVARS];
int             cvar_numIndexes;

#define FILE_HASH_SIZE      512
static convar_t  *hashTable[FILE_HASH_SIZE];

convar_t         *Cvar_Set2(const char *var_name, const char *value, qboolean force);

/*
================
return a hash value for the filename
================
*/
static long generateHashValue(const char *fname)
{
	int             i;
	long            hash;
	char            letter;

	if(!fname)
	{
		Com_Error(ERR_DROP, "null name in generateHashValue");	//gjd
	}
	hash = 0;
	i = 0;
	while(fname[i] != '\0')
	{
		letter = idStr::ToLower(fname[i]);
		hash += (long)(letter) * (i + 119);
		i++;
	}
	hash &= (FILE_HASH_SIZE - 1);
	return hash;
}

/*
============
Cvar_ValidateString
============
*/
static qboolean Cvar_ValidateString(const char *s, qboolean isvalue)
{
	if(!s)
	{
		return qfalse;
	}
	if(strchr(s, '\\') && !isvalue)
	{
		return qfalse;
	}
	if(strchr(s, '\"'))
	{
		return qfalse;
	}
	if(strchr(s, ';'))
	{
		return qfalse;
	}
	return qtrue;
}

/*
============
Cvar_FindVar
============
*/
convar_t  *Cvar_FindVar(const char *var_name)
{
	convar_t         *var;
	long            hash;

	hash = generateHashValue(var_name);

	for(var = hashTable[hash]; var; var = var->hashNext)
	{
		if(!Q_stricmp(var_name, var->name))
		{
			return var;
		}
	}

	return NULL;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue(const char *var_name)
{
	convar_t         *var;

	var = Cvar_FindVar(var_name);
	if(!var)
	{
		return 0;
	}
	return var->value;
}


/*
============
Cvar_VariableIntegerValue
============
*/
int Cvar_VariableIntegerValue(const char *var_name)
{
	convar_t         *var;

	var = Cvar_FindVar(var_name);
	if(!var)
	{
		return 0;
	}
	return var->integer;
}


/*
============
Cvar_VariableString
============
*/
char           *Cvar_VariableString(const char *var_name)
{
	convar_t         *var;

	var = Cvar_FindVar(var_name);
	if(!var)
	{
		return "";
	}
	return var->string;
}


/*
============
Cvar_VariableStringBuffer
============
*/
void Cvar_VariableStringBuffer(const char *var_name, char *buffer, int bufsize)
{
	convar_t         *var;

	var = Cvar_FindVar(var_name);
	if(!var)
	{
		*buffer = 0;
	}
	else
	{
		Q_strncpyz(buffer, var->string, bufsize);
	}
}

/*
============
Cvar_VariableStringBuffer
============
*/
void Cvar_LatchedVariableStringBuffer(const char *var_name, char *buffer, int bufsize)
{
	convar_t         *var;

	var = Cvar_FindVar(var_name);
	if(!var)
	{
		*buffer = 0;
	}
	else
	{
		if(var->latchedString)
		{
			Q_strncpyz(buffer, var->latchedString, bufsize);
		}
		else
		{
			Q_strncpyz(buffer, var->string, bufsize);
		}
	}
}

/*
============
Cvar_Flags
============
*/
int Cvar_Flags(const char *var_name)
{
	convar_t *var;
	
	if(! (var = Cvar_FindVar(var_name)) )
		return CVAR_NONEXISTENT;
	else
		return var->flags;
}



/*
============
Cvar_CommandCompletion
============
*/
void Cvar_CommandCompletion(void (*callback) (const char *s))
{
	convar_t         *cvar;

	for(cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		callback(cvar->name);
	}
}

/*
============
Cvar_ClearForeignCharacters
some cvar values need to be safe from foreign characters
============
*/
char           *Cvar_ClearForeignCharacters(const char *value)
{
	static char     clean[MAX_CVAR_VALUE_STRING];
	int             i, j;

	j = 0;
	for(i = 0; value[i] != '\0'; i++)
	{
		//if( !(value[i] & 128) )
		if(((byte *) value)[i] != 0xFF && (((byte *) value)[i] <= 127 || ((byte *) value)[i] >= 161))
		{
			clean[j] = value[i];
			j++;
		}
	}
	clean[j] = '\0';

	return clean;
}


typedef void (*setpair_t)( const char *key, const char *value, void *buffer, void *numpairs );

/*
============
Cvar_LookupVars
============
*/
void Cvar_LookupVars( int checkbit, void *buffer, void *ptr, setpair_t callback )
{
	convar_t	*cvar;

	// nothing to process ?
	if( !callback ) return;

	// force checkbit to 0 for lookup all cvars
	for( cvar = cvar_vars; cvar; cvar = cvar->next )
	{
		if( checkbit && !( cvar->flags & checkbit )) {
			continue;
		}

		if( buffer )
		{
			callback( cvar->name, cvar->string, buffer, ptr );
		}
		else
		{
			callback( cvar->name, cvar->string, cvar->description, ptr );
		}
	}
}

/*
============
Cvar_Get

If the variable already exists, the value will not be set unless CVAR_ROM
The flags will be or'ed in if the variable exists.
============
*/
convar_t *Cvar_Get( const char *var_name, const char *var_value, int flags, const char *var_desc )
{
	convar_t	*var;
	long        hash;

	if( !var_name )
	{
		Sys_Error( "Cvar_Get: passed NULL name\n" );
		return NULL;
	}

	if( !var_value ) var_value = "0"; // just apply default value

	// all broadcast cvars must be passed this check
	if( flags & ( CVAR_USERINFO|CVAR_SERVERINFO ))
	{
		if( !Cvar_ValidateString( var_name, qfalse ))
		{
			Com_Printf( "invalid info cvar name string %s\n", var_name );
			return NULL;
		}

		if( !Cvar_ValidateString( var_value, qtrue ))
		{
			Com_Printf( "invalid cvar value string: %s\n", var_value );
			var_value = "0"; // just apply default value
		}
	}

	// check for command coexisting
	if( Cmd_Exists( var_name ))
	{
		Com_Printf( "Cvar_Get: %s is a command\n", var_name );
		return NULL;
	}

	var = Cvar_FindVar( var_name );
	if( var )
	{
		// if the C code is now specifying a variable that the user already
		// set a value for, take the new value as the reset value
		if(( var->flags & CVAR_USER_CREATED ) && !( flags & CVAR_USER_CREATED ) && var_value[0] )
		{
			var->flags &= ~CVAR_USER_CREATED;
			Z_Free( var->resetString );
			var->resetString = CopyString( var_value );

			// ZOID--needs to be set so that cvars the game sets as
			// SERVERINFO get sent to clients
			cvar_modifiedFlags |= flags;
		}

		var->flags |= flags;

		// only allow one non-empty reset string without a warning
		if( !var->resetString[0] )
		{
			// we don't have a reset string yet
			Z_Free( var->resetString );
			var->resetString = CopyString( var_value );
		}

		// if we have a latched string, take that value now
		if( var->latchedString )
		{
			char *s;

			s = var->latchedString;
			var->latchedString = NULL; // otherwise cvar_set2 would free it
			Cvar_Set2( var_name, s, qtrue );
			Z_Free( s );
		}

		// TTimo
		// if CVAR_USERINFO was toggled on for an existing cvar, check wether the value needs to be cleaned from foreigh characters
		// (for instance, seta name "name-with-foreign-chars" in the config file, and toggle to CVAR_USERINFO happens later in CL_Init)
		if(flags & CVAR_USERINFO)
		{
			char           *cleaned = Cvar_ClearForeignCharacters(var->string);	// NOTE: it is probably harmless to call Cvar_Set2 in all cases, but I don't want to risk it

			if(strcmp(var->string, cleaned))
			{
				Cvar_Set2(var->name, var->string, qfalse);	// call Cvar_Set2 with the value to be cleaned up for verbosity
			}
		}

		if( var_desc )
		{
			// update description if needs
			if( var->description ) {
				Z_Free( var->description );
			}
			var->description = CopyString( var_desc );
		}
		return var;
	}

	//
	// allocate a new cvar
	//
	if(cvar_numIndexes >= MAX_CVARS)
	{
		Com_Error(ERR_FATAL, "MAX_CVARS (%d) hit -- too many cvars!", MAX_CVARS);
	}
	var = &cvar_indexes[cvar_numIndexes];
	cvar_numIndexes++;
	var->name = CopyString( var_name );
	var->string = CopyString( var_value );
	var->resetString = CopyString( var_value );
	if( var_desc ) {
		var->description = CopyString( var_desc );
	}
	var->value = atof( var->string );
	var->integer = atoi( var->string );
	var->modified = qtrue;
	var->modificationCount = 1;
	var->value = atof(var->string);
	var->integer = atoi(var->string);
	var->resetString = CopyString(var_value);

	// link the variable in
	var->next = cvar_vars;
	cvar_vars = var;

	var->flags = flags;

	hash = generateHashValue(var_name);
	var->hashNext = hashTable[hash];
	hashTable[hash] = var;

	return var;
}

/*
============
Cvar_Set2
============
*/
#define FOREIGN_MSG "Foreign characters are not allowed in userinfo variables.\n"
#ifndef DEDICATED
const char     *CL_TranslateStringBuf(const char *string);
#endif
convar_t         *Cvar_Set2(const char *var_name, const char *value, qboolean force)
{
	convar_t         *var;

	if (strcmp("com_hunkused", var_name)!=0) {
		Com_DPrintf("Cvar_Set2: %s %s\n", var_name, value);
	}

	if(!Cvar_ValidateString(var_name, qfalse))
	{
		Com_Printf("invalid cvar name string: %s\n", var_name);
		var_name = "BADNAME";
	}

	var = Cvar_FindVar(var_name);
	if(!var)
	{
		if(!value)
		{
			return NULL;
		}
		// create it
		if(!force)
		{
			return Cvar_Get(var_name, value, CVAR_USER_CREATED, NULL);
		}
		else
		{
			return Cvar_Get(var_name, value, 0, NULL);
		}
	}

	if(!value)
	{
		value = var->resetString;
	}

	if(var->flags & CVAR_USERINFO)
	{
		char           *cleaned = Cvar_ClearForeignCharacters(value);

		if(strcmp(value, cleaned))
		{
#ifdef DEDICATED
			Com_Printf(FOREIGN_MSG);
#else
			Com_Printf("%s", CL_TranslateStringBuf(FOREIGN_MSG));
#endif
			Com_Printf("Using %s instead of %s\n", cleaned, value);
			return Cvar_Set2(var_name, cleaned, force);
		}
	}

	if(!strcmp(value, var->string))
	{
		if((var->flags & CVAR_LATCH) && var->latchedString)
		{
			if(!strcmp(value, var->latchedString))
			{
				return var;
			}
		}
		else
		{
			return var;
		}
	}
	// note what types of cvars have been modified (userinfo, archive, serverinfo, systeminfo)
	cvar_modifiedFlags |= var->flags;

	if(!force)
	{
		// ydnar: don't set unsafe variables when com_crashed is set
		if((var->flags & CVAR_UNSAFE) && com_crashed != NULL && com_crashed->integer)
		{
			Com_Printf("%s is unsafe. Check com_crashed.\n", var_name);
			return var;
		}

		if(var->flags & CVAR_ROM)
		{
			Com_Printf("%s is read only.\n", var_name);
			return var;
		}

		if(var->flags & CVAR_INIT)
		{
			Com_Printf("%s is write protected.\n", var_name);
			return var;
		}

		if((var->flags & CVAR_CHEAT) && !cvar_cheats->integer)
		{
			Com_Printf("%s is cheat protected.\n", var_name);
			return var;
		}

		if ( var->flags & CVAR_SHADER )
		{
			Com_Printf( "%s will be changed upon recompiling shaders.\n", var_name );
			Cvar_Set( "r_recompileShaders", "1" );
		}

		if(var->flags & CVAR_LATCH)
		{
			if(var->latchedString)
			{
				if(strcmp(value, var->latchedString) == 0)
				{
					return var;
				}
				Z_Free(var->latchedString);
			}
			else
			{
				if(strcmp(value, var->string) == 0)
				{
					return var;
				}
			}

			Com_Printf("%s will be changed upon restarting.\n", var_name);
			var->latchedString = CopyString(value);
			var->modified = qtrue;
			var->modificationCount++;
			return var;
		}
	}
	else
	{
		if(var->latchedString)
		{
			Z_Free(var->latchedString);
			var->latchedString = NULL;
		}
	}

	if(!strcmp(value, var->string))
	{
		return var;				// not changed

	}
	var->modified = qtrue;
	var->modificationCount++;

	Z_Free(var->string);		// free the old value string

	var->string = CopyString(value);
	var->value = atof(var->string);
	var->integer = atoi(var->string);

	return var;
}

/*
============
Cvar_Set
============
*/
void Cvar_Set(const char *var_name, const char *value)
{
	Cvar_Set2(var_name, value, qtrue);
}

/*
============
Cvar_SetLatched
============
*/
void Cvar_SetLatched(const char *var_name, const char *value)
{
	Cvar_Set2(var_name, value, qfalse);
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue(const char *var_name, float value)
{
	char            val[32];

	if(value == (int)value)
	{
		Com_sprintf(val, sizeof(val), "%i", (int)value);
	}
	else
	{
		Com_sprintf(val, sizeof(val), "%f", value);
	}
	Cvar_Set(var_name, val);
}

/*
============
Cvar_SetValueSafe
============
*/
void Cvar_SetValueSafe( const char *var_name, float value) {
    char    val[32];

    if ( value == (int)value ) {
        Com_sprintf (val, sizeof(val), "%i",(int)value);
    } else {
        Com_sprintf (val, sizeof(val), "%f",value);
    }
    Cvar_Set2 (var_name, val,qfalse);
}



/*
============
Cvar_SetValueLatched
============
*/
void Cvar_SetValueLatched( const char *var_name, float value) {
	char	val[32];

	if ( value == (int)value ) {
		Com_sprintf (val, sizeof(val), "%i",(int)value);
	} else {
		Com_sprintf (val, sizeof(val), "%f",value);
	}
	Cvar_Set2 (var_name, val,qfalse);
}

/*
============
Cvar_Reset
============
*/
void Cvar_Reset(const char *var_name)
{
	Cvar_Set2(var_name, NULL, qfalse);
}


/*
============
Cvar_SetCheatState

Any testing variables will be reset to the safe values
============
*/
void Cvar_SetCheatState(void)
{
	convar_t         *var;

	// set all default vars to the safe value
	for(var = cvar_vars; var; var = var->next)
	{
		if(var->flags & CVAR_CHEAT)
		{
			if(strcmp(var->resetString, var->string))
			{
				Cvar_Set(var->name, var->resetString);
			}
		}
	}
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_Command(void)
{
	convar_t         *v;

	// check variables
	v = Cvar_FindVar(Cmd_Argv(0));
	if(!v)
	{
		return qfalse;
	}

	// perform a variable print or set
	if(Cmd_Argc() == 1)
	{
		Com_Printf("\"%s\" is:\"%s" S_COLOR_WHITE "\" default:\"%s" S_COLOR_WHITE "\"\n", v->name, v->string, v->resetString);
		if(v->latchedString)
		{
			Com_Printf("latched: \"%s\"\n", v->latchedString);
		}
		return qtrue;
	}

	// set the value if forcing isn't required
	Cvar_Set2(v->name, Cmd_Argv(1), qfalse);
	return qtrue;
}


/*
============
Cvar_Toggle_f

Toggles a cvar for easy single key binding,
optionally through a list of given values
============
*/
void Cvar_Toggle_f(void)
{
	int i, c;
	const char *varname, *curval;

	c = Cmd_Argc();
	if(c < 2)
	{
		Com_Printf("usage: toggle <variable> [<value> ...]\n");
		return;
	}

	varname = Cmd_Argv(1);

	if(c == 2)
	{
		Cvar_Set2(varname, va("%d", !Cvar_VariableValue(varname)), qfalse);
		return;
	}

	curval = Cvar_VariableString(Cmd_Argv(1));

	// don't bother checking the last value for a match, since the desired
	//  behaviour is the same as if the last value didn't match:
	//  set the variable to the first value
	for(i = 2; i < c - 1; ++i)
	{
		if(!strcmp(curval, Cmd_Argv(i)))
		{
			Cvar_Set2(varname, Cmd_Argv(i + 1), qfalse);
			return;
		}
	}

	// fallback
	Cvar_Set2(varname, Cmd_Argv(2), qfalse);
}


/*
============
Cvar_Cycle_f - ydnar

Cycles a cvar for easy single key binding
============
*/
void Cvar_Cycle_f(void)
{
	int             start, end, step, oldvalue, value;


	if(Cmd_Argc() < 4 || Cmd_Argc() > 5)
	{
		Com_Printf("usage: cycle <variable> <start> <end> [step]\n");
		return;
	}

	oldvalue = value = Cvar_VariableValue(Cmd_Argv(1));
	start = atoi(Cmd_Argv(2));
	end = atoi(Cmd_Argv(3));

	if(Cmd_Argc() == 5)
	{
		step = abs(atoi(Cmd_Argv(4)));
	}
	else
	{
		step = 1;
	}

	if(abs(end - start) < step)
	{
		step = 1;
	}

	if(end < start)
	{
		value -= step;
		if(value < end)
		{
			value = start - (step - (oldvalue - end + 1));
		}
	}
	else
	{
		value += step;
		if(value > end)
		{
			value = start + (step - (end - oldvalue + 1));
		}
	}

	Cvar_Set2(Cmd_Argv(1), va("%i", value), qfalse);
}


/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console, even if they
weren't declared in C code.
============
*/
void Cvar_Set_f(void) {
	int             c, unsafe = 0;
	char            *value;

	c = Cmd_Argc();
	if(c < 3)
	{
		Com_Printf("usage: set <variable> <value> [unsafe]\n");
		return;
	}

	// ydnar: handle unsafe vars
	if(c >= 4 && !strcmp(Cmd_Argv(c - 1), "unsafe"))
	{
		c--;
		unsafe = 1;
		if(com_crashed != NULL && com_crashed->integer)
		{
			Com_Printf("%s is unsafe. Check com_crashed.\n", Cmd_Argv(1));
			return;
		}
	}

	value = strdup(Cmd_Cmd_FromNth (2)); // 3rd arg onwards, raw

	if (unsafe)
	{
		char *end = value + strlen (value);
		// skip spaces
		while (--end > value)
			if (*end != ' ')
				break;
		++end;
		// skip "unsafe" (may be quoted, so just scan it)
		while (--end > value)
			if (*end == ' ')
				break;
		++end;
		// skip spaces
		while (--end > value)
			if (*end != ' ')
				break;
		end[1] = 0; // end of string :-)
	}

	Cvar_Set2(Cmd_Argv(1), Com_UnquoteStr(value), qfalse); 
	free (value);
}

/*
============
Cvar_SetU_f

As Cvar_Set, but also flags it as serverinfo
============
*/
void Cvar_SetU_f(void)
{
	convar_t         *v;

	if(Cmd_Argc() != 3 && Cmd_Argc() != 4)
	{
		Com_Printf("usage: setu <variable> <value> [unsafe]\n");
		return;
	}
	Cvar_Set_f();
	v = Cvar_FindVar(Cmd_Argv(1));
	if(!v)
	{
		return;
	}
	v->flags |= CVAR_USERINFO;
}

/*
============
Cvar_SetS_f

As Cvar_Set, but also flags it as serverinfo
============
*/
void Cvar_SetS_f(void)
{
	convar_t         *v;

	if(Cmd_Argc() != 3 && Cmd_Argc() != 4)
	{
		Com_Printf("usage: sets <variable> <value> [unsafe]\n");
		return;
	}
	Cvar_Set_f();
	v = Cvar_FindVar(Cmd_Argv(1));
	if(!v)
	{
		return;
	}
	v->flags |= CVAR_SERVERINFO;
}

/*
============
Cvar_SetA_f

As Cvar_Set, but also flags it as archived
============
*/
void Cvar_SetA_f(void)
{
	convar_t         *v;

	if(Cmd_Argc() != 3 && Cmd_Argc() != 4)
	{
		Com_Printf("usage: seta <variable> <value> [unsafe]\n");
		return;
	}
	Cvar_Set_f();
	v = Cvar_FindVar(Cmd_Argv(1));
	if(!v)
	{
		return;
	}
	v->flags |= CVAR_ARCHIVE;
}

/*
============
Cvar_Reset_f
============
*/
void Cvar_Reset_f(void)
{
	if(Cmd_Argc() != 2)
	{
		Com_Printf("usage: reset <variable>\n");
		return;
	}
	Cvar_Reset(Cmd_Argv(1));
}

/*
============
Cvar_WriteVariables

Appends lines containing "set variable value" for all variables
with the archive flag set to qtrue.
============
*/
void Cvar_WriteVariables(fileHandle_t f)
{
	convar_t         *var;
	char            buffer[1024];

	for(var = cvar_vars; var; var = var->next)
	{
		if(Q_stricmp(var->name, "cl_cdkey") == 0)
		{
			continue;
		}
		if(var->flags & CVAR_ARCHIVE)
		{
			// write the latched value, even if it hasn't taken effect yet
			Com_sprintf(buffer, sizeof(buffer), "seta %s %s%s\n", var->name, Com_QuoteStr (var->latchedString ? var->latchedString : var->string), (var->flags & CVAR_UNSAFE) ? " unsafe" : "");
			FS_Printf(f, "%s", buffer);
		}
	}
}

/*
============
Cvar_List_f
============
*/
void Cvar_List_f(void)
{
	convar_t         *var;
	int             i;
	char           *match;

	if(Cmd_Argc() > 1)
	{
		match = Cmd_Argv(1);
	}
	else
	{
		match = NULL;
	}

	i = 0;
	for(var = cvar_vars; var; var = var->next, i++)
	{
		if(match && !Com_Filter(match, var->name, qfalse))
		{
			continue;
		}

		if(var->flags & CVAR_SERVERINFO)
		{
			Com_Printf("S");
		}
		else
		{
			Com_Printf(" ");
		}
		if(var->flags & CVAR_USERINFO)
		{
			Com_Printf("U");
		}
		else
		{
			Com_Printf(" ");
		}
		if(var->flags & CVAR_ROM)
		{
			Com_Printf("R");
		}
		else
		{
			Com_Printf(" ");
		}
		if(var->flags & CVAR_INIT)
		{
			Com_Printf("I");
		}
		else
		{
			Com_Printf(" ");
		}
		if(var->flags & CVAR_ARCHIVE)
		{
			Com_Printf("A");
		}
		else
		{
			Com_Printf(" ");
		}
		if(var->flags & CVAR_LATCH)
		{
			Com_Printf("L");
		}
		else
		{
			Com_Printf(" ");
		}
		if(var->flags & CVAR_CHEAT)
		{
			Com_Printf("C");
		}
		else
		{
			Com_Printf(" ");
		}

		Com_Printf(" %s \"%s\" %s\n", var->name, var->string, var->description);
	}

	Com_Printf("\n%i total cvars\n", i);
	Com_Printf("%i cvar indexes\n", cvar_numIndexes);
}

/*
============
Cvar_Restart_f

Resets all cvars to their hardcoded values
============
*/
void Cvar_Restart_f(void)
{
	convar_t         *var;
	convar_t        **prev;

	prev = &cvar_vars;
	while(1)
	{
		var = *prev;
		if(!var)
		{
			break;
		}

		// don't mess with rom values, or some inter-module
		// communication will get broken (com_cl_running, etc)
		if(var->flags & (CVAR_ROM | CVAR_INIT | CVAR_NORESTART))
		{
			prev = &var->next;
			continue;
		}

		// throw out any variables the user created
		if(var->flags & CVAR_USER_CREATED)
		{
			*prev = var->next;
			if(var->name)
			{
				Z_Free(var->name);
			}
			if(var->string)
			{
				Z_Free(var->string);
			}
			if(var->latchedString)
			{
				Z_Free(var->latchedString);
			}
			if(var->resetString)
			{
				Z_Free(var->resetString);
			}
			if( var->description ) {
				Z_Free( var->description );
			}
			// clear the var completely, since we
			// can't remove the index from the list
			memset(var, 0, sizeof(*var));
			continue;
		}

		Cvar_Set(var->name, var->resetString);

		prev = &var->next;
	}
}



/*
=====================
Cvar_InfoString
=====================
*/
char           *Cvar_InfoString(int bit)
{
	static char     info[MAX_INFO_STRING];
	convar_t         *var;

	info[0] = 0;

	for(var = cvar_vars; var; var = var->next)
	{
		if(var->flags & bit)
		{
			Info_SetValueForKey(info, var->name, var->string);
		}
	}
	return info;
}

/*
=====================
Cvar_InfoString_Big

  handles large info strings ( CS_SYSTEMINFO )
=====================
*/
char           *Cvar_InfoString_Big(int bit)
{
	static char     info[BIG_INFO_STRING];
	convar_t         *var;

	info[0] = 0;

	for(var = cvar_vars; var; var = var->next)
	{
		if(var->flags & bit)
		{
			Info_SetValueForKey_Big(info, var->name, var->string);
		}
	}
	return info;
}



/*
=====================
Cvar_InfoStringBuffer
=====================
*/
void Cvar_InfoStringBuffer(int bit, char *buff, int buffsize) {
	Q_strncpyz(buff, Cvar_InfoString(bit), buffsize);
}

/*
=====================
Cvar_CheckRange
=====================
*/
void Cvar_CheckRange( convar_t *var, float min, float max, qboolean integral ) {
	var->validate = qtrue;
	var->min = min;
	var->max = max;
	var->integral = integral;

	// Force an initial range check
	Cvar_Set( var->name, var->string );
}

/*
=====================
Cvar_Register

basically a slightly modified Cvar_Get for the interpreted modules
=====================
*/
void Cvar_Register(vmCvar_t * vmCvar, const char *varName, const char *defaultValue, int flags)
{
	convar_t         *cv;

	cv = Cvar_Get(varName, defaultValue, flags, "");
	if(!vmCvar)
	{
		return;
	}
	vmCvar->handle = cv - cvar_indexes;
	vmCvar->modificationCount = -1;
	Cvar_Update(vmCvar);
}


/*
=====================
Cvar_Update

updates an interpreted modules' version of a cvar
=====================
*/
void Cvar_Update(vmCvar_t * vmCvar)
{
	convar_t         *cv = NULL;	// bk001129

	assert(vmCvar);				// bk

	if((unsigned)vmCvar->handle >= cvar_numIndexes)
	{
		Com_Error(ERR_DROP, "Cvar_Update: handle %d out of range", (unsigned)vmCvar->handle);
	}

	cv = cvar_indexes + vmCvar->handle;

	if(cv->modificationCount == vmCvar->modificationCount)
	{
		return;
	}
	if(!cv->string)
	{
		return;					// variable might have been cleared by a cvar_restart
	}
	vmCvar->modificationCount = cv->modificationCount;
	// bk001129 - mismatches.
	if(strlen(cv->string) + 1 > MAX_CVAR_VALUE_STRING)
	{
		Com_Error(ERR_DROP, "Cvar_Update: src %s length %lu exceeds MAX_CVAR_VALUE_STRING(%lu)",
				  cv->string, (long unsigned)strlen(cv->string), (long unsigned)sizeof(vmCvar->string));
	}
	// bk001212 - Q_strncpyz guarantees zero padding and dest[MAX_CVAR_VALUE_STRING-1]==0
	// bk001129 - paranoia. Never trust the destination string.
	// bk001129 - beware, sizeof(char*) is always 4 (for cv->string).
	//            sizeof(vmCvar->string) always MAX_CVAR_VALUE_STRING
	//Q_strncpyz( vmCvar->string, cv->string, sizeof( vmCvar->string ) ); // id
	Q_strncpyz(vmCvar->string, cv->string, MAX_CVAR_VALUE_STRING);

	vmCvar->value = cv->value;
	vmCvar->integer = cv->integer;
}

/*
==================
Cvar_CompleteCvarName
==================
*/
void Cvar_CompleteCvarName( char *args, int argNum )
{
	if( argNum == 2 )
	{
		// Skip "<cmd> "
		char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteCommand( p, qfalse, qtrue );
	}
}



/*
============
Cvar_Init

Reads in all archived cvars
============
*/
void Cvar_Init(void)
{
	cvar_cheats = Cvar_Get("sv_cheats", "1", CVAR_ROM | CVAR_SYSTEMINFO, "test");

	Cmd_AddCommand("toggle", Cvar_Toggle_f, "test");
	Cmd_SetCommandCompletionFunc( "toggle", Cvar_CompleteCvarName );
	Cmd_AddCommand("cycle", Cvar_Cycle_f, "test");	// ydnar
	Cmd_SetCommandCompletionFunc( "cycle", Cvar_CompleteCvarName );
	Cmd_AddCommand("set", Cvar_Set_f, "test");
	Cmd_SetCommandCompletionFunc( "set", Cvar_CompleteCvarName );
	Cmd_AddCommand("sets", Cvar_SetS_f, "test");
	Cmd_SetCommandCompletionFunc( "sets", Cvar_CompleteCvarName );
	Cmd_AddCommand("setu", Cvar_SetU_f, "test");
	Cmd_SetCommandCompletionFunc( "setu", Cvar_CompleteCvarName );
	Cmd_AddCommand("seta", Cvar_SetA_f, "test");
	Cmd_SetCommandCompletionFunc( "seta", Cvar_CompleteCvarName );
	Cmd_AddCommand("reset", Cvar_Reset_f, "test");
	Cmd_SetCommandCompletionFunc( "reset", Cvar_CompleteCvarName );
	Cmd_AddCommand("cvarlist", Cvar_List_f, "test");
	Cmd_AddCommand("cvar_restart", Cvar_Restart_f, "test");

	// NERVE - SMF - can't rely on autoexec to do this
	Cvar_Get("devdll", "1", CVAR_ROM, "test");
}
