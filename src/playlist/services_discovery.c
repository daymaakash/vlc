/*****************************************************************************
 * services_discovery.c : Manage playlist services_discovery modules
 *****************************************************************************
 * Copyright (C) 1999-2004 the VideoLAN team
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <assert.h>

#include <vlc_common.h>
#include "vlc_playlist.h"
#include "vlc_events.h"
#include <vlc_services_discovery.h>
#include "playlist_internal.h"
#include "../libvlc.h"


/*
 * Services discovery
 * Basically you just listen to Service discovery event through the
 * sd's event manager.
 * That's how the playlist get's Service Discovery information
 */

/***********************************************************************
 * GetServicesNames
 ***********************************************************************/
char ** __services_discovery_GetServicesNames( vlc_object_t * p_super,
                                               char ***pppsz_longnames )
{
    return module_GetModulesNamesForCapability( "services_discovery",
                                                pppsz_longnames );
}

/***********************************************************************
 * Create
 ***********************************************************************/
services_discovery_t *vlc_sd_Create( vlc_object_t *p_super )
{
    services_discovery_t *p_sd;

    p_sd = vlc_custom_create( p_super, sizeof( *p_sd ), VLC_OBJECT_GENERIC,
                              "services discovery" );
    if( !p_sd )
        return NULL;

    vlc_event_manager_init( &p_sd->event_manager, p_sd, (vlc_object_t *)p_sd );
    vlc_event_manager_register_event_type( &p_sd->event_manager,
            vlc_ServicesDiscoveryItemAdded );
    vlc_event_manager_register_event_type( &p_sd->event_manager,
            vlc_ServicesDiscoveryItemRemoved );
    vlc_event_manager_register_event_type( &p_sd->event_manager,
            vlc_ServicesDiscoveryStarted );
    vlc_event_manager_register_event_type( &p_sd->event_manager,
            vlc_ServicesDiscoveryEnded );

    vlc_object_attach( p_sd, p_super );

    return p_sd;
}

/***********************************************************************
 * Stop
 ***********************************************************************/
bool vlc_sd_Start ( services_discovery_t * p_sd, const char *module )
{
    assert(!p_sd->p_module);

    p_sd->p_module = module_need( p_sd, "services_discovery", module, true );

    if( p_sd->p_module == NULL )
    {
        msg_Err( p_sd, "no suitable services discovery module" );
        return false;
    }
        
    vlc_event_t event = {
        .type = vlc_ServicesDiscoveryStarted
    };
    vlc_event_send( &p_sd->event_manager, &event );
    return true;
}
                          
/***********************************************************************
 * Stop
 ***********************************************************************/
void vlc_sd_Stop ( services_discovery_t * p_sd )
{
    vlc_event_t event = {
        .type = vlc_ServicesDiscoveryEnded
    };
    
    vlc_event_send( &p_sd->event_manager, &event );

    module_unneed( p_sd, p_sd->p_module );
    p_sd->p_module = NULL;
}

/***********************************************************************
 * GetLocalizedName
 ***********************************************************************/
char *
services_discovery_GetLocalizedName ( services_discovery_t * p_sd )
{
    return strdup( module_get_name( p_sd->p_module, true ) );
}

/***********************************************************************
 * EventManager
 ***********************************************************************/
vlc_event_manager_t *
services_discovery_EventManager ( services_discovery_t * p_sd )
{
    return &p_sd->event_manager;
}

/***********************************************************************
 * AddItem
 ***********************************************************************/
void
services_discovery_AddItem ( services_discovery_t * p_sd, input_item_t * p_item,
                             const char * psz_category )
{
    vlc_event_t event;
    event.type = vlc_ServicesDiscoveryItemAdded;
    event.u.services_discovery_item_added.p_new_item = p_item;
    event.u.services_discovery_item_added.psz_category = psz_category;

    vlc_event_send( &p_sd->event_manager, &event );
}

/***********************************************************************
 * RemoveItem
 ***********************************************************************/
void
services_discovery_RemoveItem ( services_discovery_t * p_sd, input_item_t * p_item )
{
    vlc_event_t event;
    event.type = vlc_ServicesDiscoveryItemRemoved;
    event.u.services_discovery_item_removed.p_item = p_item;

    vlc_event_send( &p_sd->event_manager, &event );
}

/*
 * Playlist - Services discovery bridge
 */

 /* A new item has been added to a certain sd */
static void playlist_sd_item_added( const vlc_event_t * p_event, void * user_data )
{
    input_item_t * p_input = p_event->u.services_discovery_item_added.p_new_item;
    const char * psz_cat = p_event->u.services_discovery_item_added.psz_category;
    playlist_item_t *p_new_item, * p_parent = user_data;
    playlist_t * p_playlist = p_parent->p_playlist;

    msg_Dbg( p_playlist, "Adding %s in %s",
                p_input->psz_name ? p_input->psz_name : "(null)",
                psz_cat ? psz_cat : "(null)" );

    PL_LOCK;
    /* If p_parent is in root category (this is clearly a hack) and we have a cat */
    if( !EMPTY_STR(psz_cat) &&
        p_parent->p_parent == p_playlist->p_root_category )
    {
        /* */
        playlist_item_t * p_cat;
        p_cat = playlist_ChildSearchName( p_parent, psz_cat );
        if( !p_cat )
        {
            p_cat = playlist_NodeCreate( p_playlist, psz_cat,
                                         p_parent, 0, NULL );
            p_cat->i_flags &= ~PLAYLIST_SKIP_FLAG;
        }
        p_parent = p_cat;
    }

    p_new_item = playlist_NodeAddInput( p_playlist, p_input, p_parent,
                                        PLAYLIST_APPEND, PLAYLIST_END, pl_Locked );
    if( p_new_item )
    {
        p_new_item->i_flags &= ~PLAYLIST_SKIP_FLAG;
        p_new_item->i_flags &= ~PLAYLIST_SAVE_FLAG;
    }
    PL_UNLOCK;
}

 /* A new item has been removed from a certain sd */
static void playlist_sd_item_removed( const vlc_event_t * p_event, void * user_data )
{
    input_item_t * p_input = p_event->u.services_discovery_item_removed.p_item;
    playlist_item_t * p_parent = user_data;
    playlist_item_t * p_pl_item;

    /* First make sure that if item is a node it will be deleted.
     * XXX: Why don't we have a function to ensure that in the playlist code ? */
    vlc_object_lock( p_parent->p_playlist );
    p_pl_item = playlist_ItemFindFromInputAndRoot( p_parent->p_playlist,
            p_input->i_id, p_parent, false );

    if( p_pl_item && p_pl_item->i_children > -1 )
    {
        playlist_NodeDelete( p_parent->p_playlist, p_pl_item, true, false );
        vlc_object_unlock( p_parent->p_playlist );
        return;
    }

    /* Delete the non-node item normally */
    playlist_DeleteFromInputInParent( p_parent->p_playlist, p_input->i_id,
                                      p_parent, pl_Locked );

    vlc_object_unlock( p_parent->p_playlist );
}

int playlist_ServicesDiscoveryAdd( playlist_t *p_playlist, const char *psz_modules )
{
    const char *psz_parser = psz_modules ?: "";

    for (;;)
    {
        struct playlist_services_discovery_support_t * p_sds;
        playlist_item_t * p_cat;
        playlist_item_t * p_one;

        while( *psz_parser == ' ' || *psz_parser == ':' || *psz_parser == ',' )
            psz_parser++;

        if( *psz_parser == '\0' )
            break;

        const char *psz_next = strchr( psz_parser, ':' );
        if( psz_next == NULL )
            psz_next = psz_parser + strlen( psz_parser );

        char psz_plugin[psz_next - psz_parser + 1];
        memcpy (psz_plugin, psz_parser, sizeof (psz_plugin) - 1);
        psz_plugin[sizeof (psz_plugin) - 1] = '\0';
        psz_parser = psz_next;

        /* Perform the addition */
        msg_Dbg( p_playlist, "Add services_discovery %s", psz_plugin );
        services_discovery_t *p_sd;

        p_sd = vlc_sd_Create( (vlc_object_t*)p_playlist );
        if( !p_sd )
            return VLC_ENOMEM;

        /* Free in playlist_ServicesDiscoveryRemove */
        p_sds = malloc( sizeof(struct playlist_services_discovery_support_t) );
        if( !p_sds )
        {
            vlc_sd_Destroy( p_sd );
            return VLC_ENOMEM;
        }

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemAdded,
                          playlist_sd_item_added,
                          p_one );
        
        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemAdded,
                          playlist_sd_item_added,
                          p_cat );

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemRemoved,
                          playlist_sd_item_removed,
                          p_one );

        vlc_event_attach( services_discovery_EventManager( p_sd ),
                          vlc_ServicesDiscoveryItemRemoved,
                          playlist_sd_item_removed,
                          p_cat );

        if( !vlc_sd_Start( p_sd, psz_plugin ) )
        {
            vlc_sd_Destroy( p_sd );
            free( p_sds );
            return VLC_EGENERIC;
        }        

        char *psz = services_discovery_GetLocalizedName( p_sd );
        assert( psz );
        PL_LOCK;
        playlist_NodesPairCreate( p_playlist, psz,
                &p_cat, &p_one, false );
        PL_UNLOCK;
        free( psz );


        /* We want tree-view for service directory */
        p_one->p_input->b_prefers_tree = true;
        p_sds->p_sd = p_sd;
        p_sds->p_one = p_one;
        p_sds->p_cat = p_cat;

        PL_LOCK;
        TAB_APPEND( pl_priv(p_playlist)->i_sds, pl_priv(p_playlist)->pp_sds, p_sds );
        PL_UNLOCK;

    }

    return VLC_SUCCESS;
}

int playlist_ServicesDiscoveryRemove( playlist_t * p_playlist,
                                      const char *psz_module )
{
    struct playlist_services_discovery_support_t * p_sds = NULL;
    int i;

    PL_LOCK;
    for( i = 0 ; i< pl_priv(p_playlist)->i_sds ; i ++ )
    {
        if( !strcmp( psz_module, module_get_object( pl_priv(p_playlist)->pp_sds[i]->p_sd->p_module ) ) )
        {
            p_sds = pl_priv(p_playlist)->pp_sds[i];
            REMOVE_ELEM( pl_priv(p_playlist)->pp_sds, pl_priv(p_playlist)->i_sds, i );
            break;
        }
    }
    PL_UNLOCK;

    if( !p_sds )
    {
        msg_Warn( p_playlist, "module %s is not loaded", psz_module );
        return VLC_EGENERIC;
    }

    services_discovery_t *p_sd = p_sds->p_sd;
    assert( p_sd );

    vlc_sd_Stop( p_sd );

    vlc_event_detach( services_discovery_EventManager( p_sd ),
                        vlc_ServicesDiscoveryItemAdded,
                        playlist_sd_item_added,
                        p_sds->p_one );

    vlc_event_detach( services_discovery_EventManager( p_sd ),
                        vlc_ServicesDiscoveryItemAdded,
                        playlist_sd_item_added,
                        p_sds->p_cat );

    vlc_event_detach( services_discovery_EventManager( p_sd ),
                        vlc_ServicesDiscoveryItemRemoved,
                        playlist_sd_item_removed,
                        p_sds->p_one );

    vlc_event_detach( services_discovery_EventManager( p_sd ),
                        vlc_ServicesDiscoveryItemRemoved,
                        playlist_sd_item_removed,
                        p_sds->p_cat );

    /* Remove the sd playlist node if it exists */
    PL_LOCK;
    if( p_sds->p_cat != p_playlist->p_root_category &&
        p_sds->p_one != p_playlist->p_root_onelevel )
    {
        playlist_NodeDelete( p_playlist, p_sds->p_cat, true, false );
        playlist_NodeDelete( p_playlist, p_sds->p_one, true, false );
    }
    PL_UNLOCK;

    vlc_sd_Destroy( p_sd );
    free( p_sds );

    return VLC_SUCCESS;
}

bool playlist_IsServicesDiscoveryLoaded( playlist_t * p_playlist,
                                              const char *psz_module )
{
    int i;
    PL_LOCK;

    for( i = 0 ; i< pl_priv(p_playlist)->i_sds ; i ++ )
    {
        if( !strcmp( psz_module, module_get_object( pl_priv(p_playlist)->pp_sds[i]->p_sd->p_module ) ) )
        {
            PL_UNLOCK;
            return true;
        }
    }
    PL_UNLOCK;
    return false;
}

void playlist_ServicesDiscoveryKillAll( playlist_t *p_playlist )
{
    while( pl_priv(p_playlist)->i_sds > 0 )
        playlist_ServicesDiscoveryRemove( p_playlist,
                                          module_get_object( pl_priv(p_playlist)->pp_sds[0]->p_sd->p_module ) );
}
