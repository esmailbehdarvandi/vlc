/*****************************************************************************
 * avi.c : AVI file Stream input module for vlc
 *****************************************************************************
 * Copyright (C) 2001 VideoLAN
 * $Id: avi.c,v 1.6 2002/04/27 16:13:23 fenrir Exp $
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>                                              /* strdup() */
#include <errno.h>
#include <sys/types.h>

#include <videolan/vlc.h>

#include "stream_control.h"
#include "input_ext-intf.h"
#include "input_ext-dec.h"
#include "input_ext-plugins.h"

#include "video.h"

/*****************************************************************************
 * Constants
 *****************************************************************************/

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static void input_getfunctions( function_list_t * p_function_list );
static int  AVIDemux         ( struct input_thread_s * );
static int  AVIInit          ( struct input_thread_s * );
static void AVIEnd           ( struct input_thread_s * );

/*****************************************************************************
 * Build configuration tree.
 *****************************************************************************/
MODULE_CONFIG_START
MODULE_CONFIG_STOP

MODULE_INIT_START
    SET_DESCRIPTION( "RIFF-AVI Stream input" )
    ADD_CAPABILITY( DEMUX, 150 )
    ADD_SHORTCUT( "avi" )
MODULE_INIT_STOP

MODULE_ACTIVATE_START
    input_getfunctions( &p_module->p_functions->demux );
MODULE_ACTIVATE_STOP

MODULE_DEACTIVATE_START
MODULE_DEACTIVATE_STOP

/*****************************************************************************
 * Definition of structures and libraries for this plugins 
 *****************************************************************************/
#include "libLE.c"
#include "libioRIFF.c"
#include "avi.h"

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
static void input_getfunctions( function_list_t * p_function_list )
{
#define input p_function_list->functions.demux
    input.pf_init             = AVIInit;
    input.pf_end              = AVIEnd;
    input.pf_demux            = AVIDemux;
    input.pf_rewind           = NULL;
#undef input
}

/********************************************************************/


static void __AVIFreeDemuxData( input_thread_t *p_input )
{
    int i;
    demux_data_avi_file_t *p_avi_demux;
    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data  ; 
    
    if( p_avi_demux->p_riff != NULL ) 
            RIFF_DeleteChunk( p_input, p_avi_demux->p_riff );
    if( p_avi_demux->p_hdrl != NULL ) 
            RIFF_DeleteChunk( p_input, p_avi_demux->p_hdrl );
    if( p_avi_demux->p_movi != NULL ) 
            RIFF_DeleteChunk( p_input, p_avi_demux->p_movi );
    if( p_avi_demux->p_idx1 != NULL ) 
            RIFF_DeleteChunk( p_input, p_avi_demux->p_idx1 );
    if( p_avi_demux->pp_info != NULL )
    {
        for( i = 0; i < p_avi_demux->i_streams; i++ )
        {
            if( p_avi_demux->pp_info[i] != NULL ) 
            {
#define p_info p_avi_demux->pp_info[i]
                
                if( p_info->p_index != NULL )
                {
                      free( p_info->p_index );
                }
                free( p_info ); 
#undef  p_info
            }
        }
         free( p_avi_demux->pp_info );
    }
}

static void __AVI_Parse_avih( MainAVIHeader_t *p_avih, byte_t *p_buff )
{
    p_avih->i_microsecperframe = __GetDoubleWordLittleEndianFromBuff( p_buff );
    p_avih->i_maxbytespersec = __GetDoubleWordLittleEndianFromBuff( p_buff + 4);
    p_avih->i_reserved1 = __GetDoubleWordLittleEndianFromBuff( p_buff + 8);
    p_avih->i_flags = __GetDoubleWordLittleEndianFromBuff( p_buff + 12);
    p_avih->i_totalframes = __GetDoubleWordLittleEndianFromBuff( p_buff + 16);
    p_avih->i_initialframes = __GetDoubleWordLittleEndianFromBuff( p_buff + 20);
    p_avih->i_streams = __GetDoubleWordLittleEndianFromBuff( p_buff + 24);
    p_avih->i_suggestedbuffersize = 
                        __GetDoubleWordLittleEndianFromBuff( p_buff + 28);
    p_avih->i_width = __GetDoubleWordLittleEndianFromBuff( p_buff + 32 );
    p_avih->i_height = __GetDoubleWordLittleEndianFromBuff( p_buff + 36 );
    p_avih->i_scale = __GetDoubleWordLittleEndianFromBuff( p_buff + 40 );
    p_avih->i_rate = __GetDoubleWordLittleEndianFromBuff( p_buff + 44 );
    p_avih->i_start = __GetDoubleWordLittleEndianFromBuff( p_buff + 48);
    p_avih->i_length = __GetDoubleWordLittleEndianFromBuff( p_buff + 52);
}

static void __AVI_Parse_Header( AVIStreamHeader_t *p_strh, byte_t *p_buff )
{
    p_strh->i_type = __GetDoubleWordLittleEndianFromBuff( p_buff );
    p_strh->i_handler = __GetDoubleWordLittleEndianFromBuff( p_buff + 4 );
    p_strh->i_flags = __GetDoubleWordLittleEndianFromBuff( p_buff + 8 );
    p_strh->i_reserved1 = __GetDoubleWordLittleEndianFromBuff( p_buff + 12);
    p_strh->i_initialframes = __GetDoubleWordLittleEndianFromBuff( p_buff + 16);
    p_strh->i_scale = __GetDoubleWordLittleEndianFromBuff( p_buff + 20);
    p_strh->i_rate = __GetDoubleWordLittleEndianFromBuff( p_buff + 24);
    p_strh->i_start = __GetDoubleWordLittleEndianFromBuff( p_buff + 28);
    p_strh->i_length = __GetDoubleWordLittleEndianFromBuff( p_buff + 32);
    p_strh->i_suggestedbuffersize = 
                        __GetDoubleWordLittleEndianFromBuff( p_buff + 36);
    p_strh->i_quality = __GetDoubleWordLittleEndianFromBuff( p_buff + 40);
    p_strh->i_samplesize = __GetDoubleWordLittleEndianFromBuff( p_buff + 44);
}

int avi_ParseBitMapInfoHeader( bitmapinfoheader_t *h, byte_t *p_data )
{
    h->i_size          = __GetDoubleWordLittleEndianFromBuff( p_data );
    h->i_width         = __GetDoubleWordLittleEndianFromBuff( p_data + 4 );
    h->i_height        = __GetDoubleWordLittleEndianFromBuff( p_data + 8 );
    h->i_planes        = __GetWordLittleEndianFromBuff( p_data + 12 );
    h->i_bitcount      = __GetWordLittleEndianFromBuff( p_data + 14 );
    h->i_compression   = __GetDoubleWordLittleEndianFromBuff( p_data + 16 );
    h->i_sizeimage     = __GetDoubleWordLittleEndianFromBuff( p_data + 20 );
    h->i_xpelspermeter = __GetDoubleWordLittleEndianFromBuff( p_data + 24 );
    h->i_ypelspermeter = __GetDoubleWordLittleEndianFromBuff( p_data + 28 );
    h->i_clrused       = __GetDoubleWordLittleEndianFromBuff( p_data + 32 );
    h->i_clrimportant  = __GetDoubleWordLittleEndianFromBuff( p_data + 36 );
    return( 0 );
}

int avi_ParseWaveFormatEx( waveformatex_t *h, byte_t *p_data )
{
    h->i_formattag     = __GetWordLittleEndianFromBuff( p_data );
    h->i_channels      = __GetWordLittleEndianFromBuff( p_data + 2 );
    h->i_samplespersec = __GetDoubleWordLittleEndianFromBuff( p_data + 4 );
    h->i_avgbytespersec= __GetDoubleWordLittleEndianFromBuff( p_data + 8 );
    h->i_blockalign    = __GetWordLittleEndianFromBuff( p_data + 12 );
    h->i_bitspersample = __GetWordLittleEndianFromBuff( p_data + 14 );
    h->i_size          = __GetWordLittleEndianFromBuff( p_data + 16 );
    return( 0 );
}

static int __AVI_ParseStreamHeader( u32 i_id, int *i_number, u16 *i_type )
{
    int c1,c2,c3,c4;

    c1 = ( i_id ) & 0xFF;
    c2 = ( i_id >>  8 ) & 0xFF;
    c3 = ( i_id >> 16 ) & 0xFF;
    c4 = ( i_id >> 24 ) & 0xFF;

    if( c1 < '0' || c1 > '9' || c2 < '0' || c2 > '9' )
    {
        return( -1 );
    }
    *i_number = (c1 - '0') * 10 + (c2 - '0' );
    *i_type = ( c4 << 8) + c3;
    return( 0 );
}   

static void __AVI_AddEntryIndex( AVIStreamInfo_t *p_info,
                                 AVIIndexEntry_t *p_index)
{
    AVIIndexEntry_t *p_tmp;
    if( p_info->p_index == NULL )
    {
        p_info->i_idxmax = 16384;
        p_info->i_idxnb = 0;
        p_info->p_index = calloc( p_info->i_idxmax, 
                                  sizeof( AVIIndexEntry_t ) );
        if( p_info->p_index == NULL ) {return;}
    }
    if( p_info->i_idxnb >= p_info->i_idxmax )
    {
        p_info->i_idxmax += 16384;
        p_tmp = realloc( (void*)p_info->p_index,
                           p_info->i_idxmax * 
                           sizeof( AVIIndexEntry_t ) );
        if( p_tmp == NULL ) 
        { 
            p_info->i_idxmax -= 16384;
            return; 
        }
        p_info->p_index = p_tmp;
    }
    /* calculate cumulate length */
    if( p_info->i_idxnb > 0 )
    {
        p_index->i_lengthtotal = p_index->i_length +
            p_info->p_index[p_info->i_idxnb-1].i_lengthtotal;
    }
    else
    {
        p_index->i_lengthtotal = 0;
    }

    p_info->p_index[p_info->i_idxnb] = *p_index;
    p_info->i_idxnb++;
}

static void __AVI_GetIndex( input_thread_t *p_input )
{
    demux_data_avi_file_t *p_avi_demux;
    AVIIndexEntry_t index;
    byte_t          *p_buff;
    riffchunk_t     *p_idx1;
    int             i_read;
    int             i;
    int             i_number;
    u16             i_type;
    int             i_totalentry = 0;
    
    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data  ;    

    if( RIFF_FindAndGotoDataChunk( p_input,
                                   p_avi_demux->p_riff, 
                                   &p_idx1, 
                                   FOURCC_idx1)!=0 )
    {
        intf_WarnMsg( 1, "input init: cannot find index" );
        RIFF_GoToChunk( p_input, p_avi_demux->p_hdrl );        
        return;
    }
    p_avi_demux->p_idx1 = p_idx1;
    intf_WarnMsg( 1, "input init: loading index" ); 
    for(;;)
    {
        if( ((i_read = input_Peek( p_input, &p_buff, 16*1024 )) < 16 )
              ||( i_totalentry *16 >= p_idx1->i_size ) )
        {
            intf_WarnMsg( 1,"input info: read %d idx chunk", i_totalentry );
            return;
        }
        i_read /= 16 ;
        /* TODO try to verify if we are beyond end of p_idx1 */
        for( i = 0; i < i_read; i++ )
        {
            byte_t  *p_peek = p_buff + i * 16;
            i_totalentry++;
            index.i_id = __GetDoubleWordLittleEndianFromBuff( p_peek );
            index.i_flags = __GetDoubleWordLittleEndianFromBuff( p_peek+4);
            index.i_offset = __GetDoubleWordLittleEndianFromBuff( p_peek+8);
            index.i_length = __GetDoubleWordLittleEndianFromBuff(p_peek+12);
            
            if( (__AVI_ParseStreamHeader( index.i_id, &i_number, &i_type ) != 0)
             ||(i_number > p_avi_demux->i_streams)) 
            {
                continue;
            }
            __AVI_AddEntryIndex( p_avi_demux->pp_info[i_number],
                                 &index );
        }
        __RIFF_SkipBytes( p_input, 16 * i_read );
    }

}
static int __AVI_SeekToChunk( input_thread_t *p_input, AVIStreamInfo_t *p_info )
{
    demux_data_avi_file_t *p_avi_demux;
    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data;
    
    if( (p_info->p_index != NULL)&&(p_info->i_idxpos < p_info->i_idxnb) )
    {
        /* perfect */
        off_t i_pos;
        i_pos = (off_t)p_info->p_index[p_info->i_idxpos].i_offset +
                    p_info->i_idxoffset;

        p_input->pf_seek( p_input, i_pos );
        input_AccessReinit( p_input );
        return( 0 );
    }
    /* no index can't arrive but ...*/
    intf_WarnMsg( 1, "input error: can't seek");
    return( -1 );
}


/* XXX call after get p_movi */
static void __AVI_GetIndexOffset( input_thread_t *p_input )
{
    riffchunk_t *p_chunk;
    demux_data_avi_file_t *p_avi_demux;
    int i;

    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data;
    for( i = 0; i < p_avi_demux->i_streams; i++ )
    {
#define p_info p_avi_demux->pp_info[i]
        if( p_info->p_index == NULL ) 
        {
            intf_WarnMsg( 1, "input demux: can't find offset for stream %d",i);
            continue;
        }
        p_info->i_idxoffset = 0;
        __AVI_SeekToChunk( p_input, p_info );
        p_chunk = RIFF_ReadChunk( p_input );
        if( (p_chunk == NULL)||(p_chunk->i_id != p_info->p_index[0].i_id) )
        {
            p_info->i_idxoffset = p_avi_demux->p_movi->i_pos + 8;
            __AVI_SeekToChunk( p_input, p_info );
            p_chunk = RIFF_ReadChunk( p_input );
            if( (p_chunk == NULL)||(p_chunk->i_id != p_info->p_index[0].i_id) )
            {
                intf_WarnMsg( 1, "input demux: can't find offset for stream %d",
                                i);
                continue; /* TODO: search manually from p_movi */
            }
        }
#undef p_info
    }
    return;
}

static int __AVI_AudioGetType( u32 i_type )
{
    switch( i_type )
    {
/*        case( WAVE_FORMAT_PCM ):
            return( WAVE_AUDIO_ES ); */
        case( WAVE_FORMAT_AC3 ):
            return( AC3_AUDIO_ES );
        case( WAVE_FORMAT_MPEG):
        case( WAVE_FORMAT_MPEGLAYER3):
            return( MPEG2_AUDIO_ES ); /* 2 for mpeg-2 layer 1 2 ou 3 */
        default:
            return( 0 );
    }
}

static int __AVI_VideoGetType( u32 i_type )
{
    switch( i_type )
    {
        case( FOURCC_DIV3 ):
        case( FOURCC_div3 ):
        case( FOURCC_DIV4 ):
        case( FOURCC_div4 ):
        case( FOURCC_DIV5 ):
        case( FOURCC_div5 ):
        case( FOURCC_DIV6 ):
        case( FOURCC_div6 ):
        case( FOURCC_3IV1 ):
        case( FOURCC_AP41 ):
        case( FOURCC_MP43 ):
        case( FOURCC_mp43 ):
            return( MSMPEG4_VIDEO_ES );

        case( FOURCC_DIVX ):
        case( FOURCC_divx ):
        case( FOURCC_DX50 ):
        case( FOURCC_MP4S ):
        case( FOURCC_MPG4 ):
        case( FOURCC_mpg4 ):
        case( FOURCC_mp4v ):
            return( MPEG4_VIDEO_ES );

        default:
            return( 0 );
    }
}
/**************************************************************************/

static int AVIInit( input_thread_t *p_input )
{
    riffchunk_t *p_riff,*p_hdrl,*p_movi;
    riffchunk_t *p_avih;
    riffchunk_t *p_strl,*p_strh,*p_strf;
    demux_data_avi_file_t *p_avi_demux;
    es_descriptor_t *p_es = NULL; /* for not warning */
    es_descriptor_t *p_es_video; 
    es_descriptor_t *p_es_audio;

    int i;

    /* we need to seek to be able to readcorrectly */
    if( !p_input->stream.b_seekable ) 
    {
        intf_ErrMsg( "input error: need the ability to seek in stream" );
        return( -1 );
    }

    p_input->p_demux_data = 
                p_avi_demux = malloc( sizeof(demux_data_avi_file_t) );
    if( p_avi_demux == NULL )
    {
        intf_ErrMsg( "input error: not enough memory" );
        return( -1 );
    }
    memset( p_avi_demux, 0, sizeof( demux_data_avi_file_t ) );

    /* FIXME I don't know what it's do, copied from ESInit */
    /* Initialize access plug-in structures. */
    if( p_input->i_mtu == 0 )
    {
        /* Improve speed. */
        p_input->i_bufsize = INPUT_DEFAULT_BUFSIZE;
    }

    if( RIFF_TestFileHeader( p_input, &p_riff, FOURCC_AVI ) != 0 )    
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input: RIFF-AVI plug-in discarded (avi_file)" );
        return( -1 );
    }
    p_avi_demux->p_riff = p_riff;

    if ( RIFF_DescendChunk(p_input) != 0 )
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot look for subchunk (avi_file)" );
        return ( -1 );
    }

    /* it's a riff-avi file, so search for LIST-hdrl */
    if( RIFF_FindListChunk(p_input ,&p_hdrl,p_riff, FOURCC_hdrl) != 0 )
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot find \"LIST-hdrl\" (avi_file)" );
        return( -1 );
    }
    p_avi_demux->p_hdrl = p_hdrl;

    if( RIFF_DescendChunk(p_input) != 0 )
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot look for subchunk (avi_file)" );
        return ( -1 );
    }
    /* in  LIST-hdrl search avih */
    if( RIFF_FindAndLoadChunk( p_input, p_hdrl, 
                                    &p_avih, FOURCC_avih ) != 0 )
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot find \"avih\" chunk (avi_file)" );
        return( -1 );
    }
    __AVI_Parse_avih( &p_avi_demux->avih, p_avih->p_data->p_payload_start );
    RIFF_DeleteChunk( p_input, p_avih );
    
    if( p_avi_demux->avih.i_streams == 0 )  
    /* no stream found, perhaps it would be cool to find it */
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: no defined stream !" );
        return( -1 );
    }

    /*  create one program */
    vlc_mutex_lock( &p_input->stream.stream_lock ); 
    if( input_InitStream( p_input, 0 ) == -1)
    {
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot init stream" );
        return( -1 );
    }
    if( input_AddProgram( p_input, 0, 0) == NULL )
    {
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot add program" );
        return( -1 );
    }
    p_input->stream.p_selected_program = p_input->stream.pp_programs[0];
    p_input->stream.p_new_program = p_input->stream.pp_programs[0] ;
    p_input->stream.i_mux_rate = p_avi_demux->avih.i_maxbytespersec / 50;
    vlc_mutex_unlock( &p_input->stream.stream_lock ); 

    /* now read info on each stream and create ES */
    p_avi_demux->i_streams = p_avi_demux->avih.i_streams;
    
    p_avi_demux->pp_info = calloc( p_avi_demux->i_streams, 
                                    sizeof( AVIStreamInfo_t* ) );
    memset( p_avi_demux->pp_info, 0, 
                        sizeof( AVIStreamInfo_t* ) * p_avi_demux->i_streams );

    for( i = 0 ; i < p_avi_demux->i_streams; i++ )
    {
#define p_info  p_avi_demux->pp_info[i]
        p_info = malloc( sizeof(AVIStreamInfo_t ) );
        memset( p_info, 0, sizeof( AVIStreamInfo_t ) );        

        if( ( RIFF_FindListChunk(p_input,
                                &p_strl,p_hdrl, FOURCC_strl) != 0 )
                ||( RIFF_DescendChunk(p_input) != 0 ))
        {
            __AVIFreeDemuxData( p_input );
            intf_ErrMsg( "input error: cannot find \"LIST-strl\" (avi_file)" );
            return( -1 );
        }
        
        /* in  LIST-strl search strh */
        if( RIFF_FindAndLoadChunk( p_input, p_hdrl, 
                                &p_strh, FOURCC_strh ) != 0 )
        {
            RIFF_DeleteChunk( p_input, p_strl );
            __AVIFreeDemuxData( p_input );
            intf_ErrMsg( "input error: cannot find \"strh\" (avi_file)" );
            return( -1 );
        }
        __AVI_Parse_Header( &p_info->header,
                        p_strh->p_data->p_payload_start);
        RIFF_DeleteChunk( p_input, p_strh );      

        /* in  LIST-strl search strf */
        if( RIFF_FindAndLoadChunk( p_input, p_hdrl, 
                                &p_strf, FOURCC_strf ) != 0 )
        {
            RIFF_DeleteChunk( p_input, p_strl );
            __AVIFreeDemuxData( p_input );
            intf_ErrMsg( "input error: cannot find \"strf\" (avi_file)" );
            return( -1 );
        }
        /* we don't get strd, it's useless for divx,opendivx,mepgaudio */ 
        if( RIFF_AscendChunk(p_input, p_strl) != 0 )
        {
            RIFF_DeleteChunk( p_input, p_strf );
            RIFF_DeleteChunk( p_input, p_strl );
            __AVIFreeDemuxData( p_input );
            intf_ErrMsg( "input error: cannot go out (\"strl\") (avi_file)" );
            return( -1 );
        }

        /* add one ES */
        vlc_mutex_lock( &p_input->stream.stream_lock );
        p_es = input_AddES( p_input,
                            p_input->stream.p_selected_program, 1+i,
                            p_strf->i_size );
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        p_es->i_stream_id =i; /* XXX: i don't use it */ 
       
        switch( p_info->header.i_type )
        {
            case( FOURCC_auds ):
                p_es->i_cat = AUDIO_ES;
                avi_ParseWaveFormatEx( &p_info->audio_format,
                                   p_strf->p_data->p_payload_start ); 
                p_es->b_audio = 1;
                p_es->i_type = 
                    __AVI_AudioGetType( p_info->audio_format.i_formattag );
                if( p_es->i_type == 0 )
                {
                    intf_ErrMsg( "input error: stream(%d,0x%x) not supported",
                                    i,
                                    p_info->audio_format.i_formattag );
                    p_es->i_cat = UNKNOWN_ES;
                }
                break;
                
            case( FOURCC_vids ):
                p_es->i_cat = VIDEO_ES;
                avi_ParseBitMapInfoHeader( &p_info->video_format,
                                   p_strf->p_data->p_payload_start ); 
                p_es->b_audio = 0;
                p_es->i_type = 
                    __AVI_VideoGetType( p_info->video_format.i_compression );
                if( p_es->i_type == 0 )
                {
                    intf_ErrMsg( "input error: stream(%d,%4.4s) not supported",
                               i,
                               (char*)&p_info->video_format.i_compression);
                    p_es->i_cat = UNKNOWN_ES;
                }
                break;
            default:
                intf_ErrMsg( "input error: unknown stream(%d) type",
                            i );
                p_es->i_cat = UNKNOWN_ES;
                break;
        }
        p_info->p_es = p_es;
        p_info->i_cat = p_es->i_cat;
        /* We copy strf for decoder in p_es->p_demux_data */
        memcpy( p_es->p_demux_data, 
                p_strf->p_data->p_payload_start,
                p_strf->i_size );
        RIFF_DeleteChunk( p_input, p_strf );
        RIFF_DeleteChunk( p_input, p_strl );
#undef p_info           
    }



    /* go out of p_hdrl */
    if( RIFF_AscendChunk(p_input, p_hdrl) != 0)
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot go out (\"hdrl\") (avi_file)" );
        return( -1 );
    }

    /* go to movi chunk to get it*/
    if( RIFF_FindListChunk(p_input ,&p_movi,p_riff, FOURCC_movi) != 0 )
    {
        intf_ErrMsg( "input error: cannot find \"LIST-movi\" (avi_file)" );
        __AVIFreeDemuxData( p_input );
        return( -1 );
    }
    p_avi_demux->p_movi = p_movi;
    
    /* get index  XXX need to have p_movi */
    if( (p_avi_demux->avih.i_flags&AVIF_HASINDEX) != 0 )
    {
        /* get index */
        __AVI_GetIndex( p_input ); 
        /* try to get i_idxoffset for each stream  */
        __AVI_GetIndexOffset( p_input );
    }
    else
    {
        intf_WarnMsg( 1, "input init: no index !" );
    }

    
    /* we verify that each stream have at least one entry or create it */
    for( i = 0; i < p_avi_demux->i_streams ; i++ )
    {
        AVIIndexEntry_t index;
        riffchunk_t     *p_chunk;
#define p_info  p_avi_demux->pp_info[i]
        if( p_info->p_index == NULL )
        {
            intf_WarnMsg( 1, "input init: add index entry for stream %d", i ); 
            RIFF_GoToChunk( p_input, p_avi_demux->p_movi );
            if( RIFF_DescendChunk(p_input) != 0 ) { continue; }
            p_chunk = NULL;
            switch( p_info->i_cat ) 
            {
                case( AUDIO_ES ):
                    p_info->i_idxoffset = 0;  /* ref: begining of file */
                    if( RIFF_FindChunk( p_input, 
                               MAKEFOURCC('0'+i/10, '0'+i%10,'w','b' ), 
                                             p_movi ) == 0)
                    {
                       p_chunk = RIFF_ReadChunk( p_input );
                    }
                    break;
                    
                case( VIDEO_ES ):
                    p_info->i_idxoffset = 0;
                    if( (RIFF_FindChunk( p_input, 
                                    MAKEFOURCC('0'+i/10, '0'+i%10,'d','c' ),
                                            p_movi ) == 0) )
                    {
                        p_chunk = RIFF_ReadChunk( p_input ); 
                    }
                    else
                    {
                        RIFF_GoToChunk( p_input, p_avi_demux->p_movi );
                        if( RIFF_DescendChunk(p_input) != 0 ) { continue; }
                        if( (RIFF_FindChunk( p_input,
                                        MAKEFOURCC('0'+i/10, '0'+i%10,'d','b' ),
                                            p_movi ) == 0) )
                        {
                            p_chunk = RIFF_ReadChunk( p_input );
                        }
                    }
                    break;
            }
            if( p_chunk != NULL )
            {
                index.i_id = p_chunk->i_id;
                index.i_flags = AVIIF_KEYFRAME;
                index.i_offset = p_chunk->i_pos;
                index.i_length = p_chunk->i_size;
                __AVI_AddEntryIndex( p_info, &index );
            }
        }
#undef p_info
    }

    /* to make sure to go the begining because unless demux will see a seek */
    RIFF_GoToChunk( p_input, p_avi_demux->p_movi );
    if( RIFF_DescendChunk( p_input ) != 0 )
    {
        __AVIFreeDemuxData( p_input );
        intf_ErrMsg( "input error: cannot go in (\"movi\") (avi_file)" );
        return( -1 );
    }

    /* print informations on streams */
    intf_Msg( "input init: AVIH: %d stream, flags %s%s%s%s%s%s ", 
            p_avi_demux->i_streams,
            p_avi_demux->avih.i_flags&AVIF_HASINDEX?" HAS_INDEX":"",
            p_avi_demux->avih.i_flags&AVIF_MUSTUSEINDEX?" MUST_USE_INDEX":"",
            p_avi_demux->avih.i_flags&AVIF_ISINTERLEAVED?" IS_INTERLEAVED":"",
            p_avi_demux->avih.i_flags&AVIF_TRUSTCKTYPE?" TRUST_CKTYPE":"",
            p_avi_demux->avih.i_flags&AVIF_WASCAPTUREFILE?" CAPTUREFILE":"",
            p_avi_demux->avih.i_flags&AVIF_COPYRIGHTED?" COPYRIGHTED":"" );

    p_es_video = NULL;
    p_es_audio = NULL;
   
    for( i = 0; i < p_avi_demux->i_streams; i++ )
    {
#define p_info  p_avi_demux->pp_info[i]
        switch( p_info->p_es->i_cat )
        {
            case( VIDEO_ES ):
                intf_Msg("input init: video(%4.4s) %dx%d %dbpp %ffps (size %d)",
                        (char*)&p_info->video_format.i_compression,
                        p_info->video_format.i_width,
                        p_info->video_format.i_height,
                        p_info->video_format.i_bitcount,
                        (float)p_info->header.i_rate /
                            (float)p_info->header.i_scale,
                        p_info->header.i_samplesize );
                if( (p_es_video == NULL) ) 
                {
                    p_es_video = p_info->p_es;
                }
                break;

            case( AUDIO_ES ):
                intf_Msg( "input init: audio(0x%x) %d channels %dHz %dbits %ffps (size %d)",
                        p_info->audio_format.i_formattag,
                        p_info->audio_format.i_channels,
                        p_info->audio_format.i_samplespersec,
                        p_info->audio_format.i_bitspersample,
                        (float)p_info->header.i_rate /
                            (float)p_info->header.i_scale,
                        p_info->header.i_samplesize );
                if( (p_es_audio == NULL) ) 
                {
                    p_es_audio = p_info->p_es;
                }
                break;
        }
#undef p_info    
    }

    /* we select the first audio and video ES */
    vlc_mutex_lock( &p_input->stream.stream_lock );
    if( p_es_video != NULL ) 
    {
        intf_WarnMsg( 1,"input init: selecting video stream %d",
                        p_es_video->i_id  );
        input_SelectES( p_input, p_es_video );
        /*  it seems that it's useless to select es because there are selected 
         *  by the interface but i'm not sure of that */
    }
    else
    {
        vlc_mutex_unlock( &p_input->stream.stream_lock );
        intf_ErrMsg( "input error: no video stream found !" );
        return( -1 );
    }
    if( p_es_audio != NULL ) 
    {
        intf_WarnMsg( 1,"input init: selecting audio stream %d",
                        p_es_audio->i_id  );
        input_SelectES( p_input, p_es_audio );
    }
    else
    {
        intf_Msg( "input init: no audio stream found !" );
    }
    p_input->stream.p_selected_program->b_is_ok = 1;
    vlc_mutex_unlock( &p_input->stream.stream_lock );

    return( 0 );
}

static void AVIEnd( input_thread_t *p_input )
{   
    __AVIFreeDemuxData( p_input ); 
    return;
}


static mtime_t __AVI_GetPTS( AVIStreamInfo_t *p_info )
{
    /* XXX you need to add p_info->i_date to have correct pts */
    /* p_info->p_index[p_info->i_idxpos] need to be valid !! */
    mtime_t i_pts;

    /* be careful to  *1000000 before round  ! */
    if( p_info->header.i_samplesize != 0 )
    {
        i_pts = (mtime_t)( (double)1000000.0 *
                    (double)p_info->p_index[p_info->i_idxpos].i_lengthtotal *
                    (double)p_info->header.i_scale /
                    (double)p_info->header.i_rate /
                    (double)p_info->header.i_samplesize );
    }
    else
    {
        i_pts = (mtime_t)( (double)1000000.0 *
                    (double)p_info->i_idxpos *
                    (double)p_info->header.i_scale /
                    (double)p_info->header.i_rate);
    }
    return( i_pts );
}



static int __AVI_NextIndexEntry( input_thread_t *p_input, 
                                  AVIStreamInfo_t *p_info )
{
    AVIIndexEntry_t index;
    riffchunk_t     *p_chunk;
    demux_data_avi_file_t *p_avi_demux;
    AVIStreamInfo_t *p_info_tmp;
    int             i;
    int             i_idxpos;
    int             b_inc = 0;
    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data;

    p_info->i_idxpos++;

    if( p_info->i_idxpos < p_info->i_idxnb )
    {
        return( 0 );
    }
    p_info->i_idxpos--;
    /* create entry on the fly */
    /* TODO: when parsing for a stream take care of the other to not do 
       the same things two time */
    /* search for the less advance stream and parse from it for all streams*/
    p_info_tmp = p_info;
    
    for( i = 0; i < p_avi_demux->i_streams; i++ )
    {
#define p_info_i p_avi_demux->pp_info[i]
        if( p_info_i->p_index[p_info_i->i_idxnb - 1].i_offset + 
                        p_info_i->i_idxoffset < 
            p_info_tmp->p_index[p_info_tmp->i_idxnb - 1].i_offset +
                        p_info_tmp->i_idxoffset )
        {
            p_info_tmp = p_info_i;
        }
#undef  p_info_i
    }
    /* go to last defined entry */
    i_idxpos = p_info_tmp->i_idxpos; /* save p_info_tmp->i_idxpos */
    p_info_tmp->i_idxpos = p_info_tmp->i_idxnb - 1;
    __AVI_SeekToChunk( p_input, p_info_tmp );
    p_info_tmp->i_idxpos = i_idxpos;

    if( RIFF_NextChunk( p_input, p_avi_demux->p_movi ) != 0 )
    {
        __AVI_SeekToChunk( p_input, p_info );
        return( -1 );
    }
    /* save idxpos of p_info */
    /* now parse for all stream and stop when reach next chunk for p_info */
    for( i = 0; (i < 20)||(!b_inc); i++)
    {
        int i_number;
        u16 i_type;
        if( (p_chunk = RIFF_ReadChunk( p_input )) == NULL )
        {
            if( i > 0)
            {
                return( 0 );
            }
            else
            {
                return( -1 );
            }
        }

        index.i_id = p_chunk->i_id;
        index.i_flags = AVIIF_KEYFRAME;
        index.i_offset = p_chunk->i_pos;
        index.i_length = p_chunk->i_size;
        RIFF_DeleteChunk( p_input, p_chunk );

#define p_info_i    p_avi_demux->pp_info[i_number]
       if( (__AVI_ParseStreamHeader( index.i_id, &i_number, &i_type ) == 0)
             &&( i_number < p_avi_demux->i_streams )
             && (p_info_i->p_index[p_info_i->i_idxnb - 1].i_offset + 
                     p_info_i->p_index[p_info_i->i_idxnb - 1].i_length <= 
                        index.i_offset ) )
        {
            /* do we need to check i_type ? */
            __AVI_AddEntryIndex( p_info_i, &index );
            if( (p_info_i == p_info)&&(!b_inc) )
            {
                p_info->i_idxpos++;
                b_inc = 1;
            }
        }
#undef  p_info_i
        if( RIFF_NextChunk( p_input, p_avi_demux->p_movi ) != 0 )
        {
            if( i > 0)
            {
                return( 0 );
            }
            else
            {
                return( -1 );
            }
        }
    } 
    return( 0 );
    intf_WarnMsg( 1, "input demux: added index entry(%d)",i );
}

static int __AVI_ReAlign( input_thread_t *p_input, 
                            AVIStreamInfo_t  *p_info )
{
    u32     u32_pos;
    off_t   i_pos;
    
    __RIFF_TellPos( p_input, &u32_pos );
     i_pos = (off_t)u32_pos - (off_t)p_info->i_idxoffset;
   
    /* TODO verifier si on est dans p_movi */
    
    if( p_info->p_index[p_info->i_idxnb-1].i_offset < i_pos )
    {
        p_info->i_idxpos = p_info->i_idxnb-1;
        while( p_info->p_index[p_info->i_idxpos].i_offset < i_pos )
        {
            if( __AVI_NextIndexEntry( p_input, p_info ) !=0 )
            {
                return( -1 );
            }
        }
        return( 0 ); 
    }
    
    if( i_pos <= p_info->p_index[0].i_offset )
    {
        p_info->i_idxpos = 0;
        return( 0 );
    }
    /* if we have seek in the current chunk then do nothing 
        __AVI_SeekToChunk will correct */
    if( (p_info->p_index[p_info->i_idxpos].i_offset <= i_pos)
            && ( i_pos < p_info->p_index[p_info->i_idxpos].i_offset + 
                    p_info->p_index[p_info->i_idxpos].i_length ) )
    {
        return( 0 );
    }

    if( i_pos >= p_info->p_index[p_info->i_idxpos].i_offset )
    {
        /* search for a chunk after i_idxpos */
        while( (p_info->p_index[p_info->i_idxpos].i_offset < i_pos)
                &&( p_info->i_idxpos < p_info->i_idxnb - 1 ) )
        {
            if( __AVI_NextIndexEntry( p_input, p_info ) != 0 )
            {
                return( -1 );
            }
        }
        while( ((p_info->p_index[p_info->i_idxpos].i_flags&AVIIF_KEYFRAME) == 0)
                &&( p_info->i_idxpos < p_info->i_idxnb - 1 ) )
        {
            if( __AVI_NextIndexEntry( p_input, p_info ) != 0 )
            {
                    return( -1 );
            }
        }
    }
    else
    {
        /* search for a chunk before i_idxpos */
        while( (p_info->p_index[p_info->i_idxpos].i_offset + 
                    p_info->p_index[p_info->i_idxpos].i_length >= i_pos)
                        &&( p_info->i_idxpos > 0 ) )
        {
            p_info->i_idxpos--; /* backward, index is always valid */
        }
        while( ((p_info->p_index[p_info->i_idxpos].i_flags&AVIIF_KEYFRAME) == 0)
                &( p_info->i_idxpos > 0 ) )
        {
            p_info->i_idxpos--;
        }
    }
    
    return( 0 );
}
static void __AVI_SynchroReInit( input_thread_t *p_input,
                                 AVIStreamInfo_t *p_info_master,
                                 AVIStreamInfo_t *p_info_slave )
{
    demux_data_avi_file_t *p_avi_demux;

    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data;
    
    p_avi_demux->i_date = mdate() + DEFAULT_PTS_DELAY 
                            - __AVI_GetPTS( p_info_master );

    if( p_info_slave != NULL )
    {
        p_info_slave->i_idxpos = 0; 
        while( __AVI_GetPTS( p_info_slave) < __AVI_GetPTS( p_info_master) )
        {
            __AVI_NextIndexEntry( p_input, p_info_slave );
        }
        if( (__AVI_GetPTS( p_info_slave) > __AVI_GetPTS( p_info_master))
            &&(p_info_slave->i_idxpos>0) )
        {
            p_info_slave->i_idxpos--;
        }
       p_info_slave->b_unselected = 0 ;
    }
    p_input->stream.p_selected_program->i_synchro_state = SYNCHRO_OK;
} 
/** -1 in case of error, 0 of EOF, 1 otherwise **/
static int AVIDemux( input_thread_t *p_input )
{
    /* on cherche un block
       plusieurs cas :
        * encapsuler dans un chunk "rec "
        * juste une succesion de 00dc 01wb ...
        * pire tout audio puis tout video ou vice versa
     */
/* TODO :   * create dynamically index for invalid or incomplete index
            * verify that we are reading in p_movi 
            * XXX be sure to send audio before video to avoid click
 */
    riffchunk_t *p_chunk;
    int i;
    pes_packet_t *p_pes;
    demux_data_avi_file_t *p_avi_demux;

    AVIStreamInfo_t *p_info_video;
    AVIStreamInfo_t *p_info_audio;
    AVIStreamInfo_t *p_info;
    /* XXX arrive pas a avoir acces a cette fct� */
/*    input_ClockManageRef( p_input,
                            p_input->stream.p_selected_program,
                            (mtime_t)0 );  ??? what suppose to do */
    p_avi_demux = (demux_data_avi_file_t*)p_input->p_demux_data;

    /* search video and audio stream selected */
    p_info_video = NULL;
    p_info_audio = NULL;
    
    for( i = 0; i < p_avi_demux->i_streams; i++ )
    {
        if( p_avi_demux->pp_info[i]->p_es->p_decoder_fifo != NULL )
        {
            switch( p_avi_demux->pp_info[i]->p_es->i_cat )
            {
                case( VIDEO_ES ):
                    p_info_video = p_avi_demux->pp_info[i];
                    break;
                case( AUDIO_ES ):
                    p_info_audio = p_avi_demux->pp_info[i];
                    break;
            }
        }
        else
        {
            p_avi_demux->pp_info[i]->b_unselected = 1;
        }
    }
    if( p_info_video == NULL )
    {
        intf_ErrMsg( "input error: no video ouput selected" );
        return( -1 );
    }
    if( (input_ClockManageControl( p_input, p_input->stream.p_selected_program,
                            (mtime_t)0) == PAUSE_S) )
    {   
        __AVI_SynchroReInit( p_input, p_info_video, p_info_audio );
    }

    /* after updated p_avi_demux->pp_info[i]->b_unselected  !! */
    if( p_input->stream.p_selected_program->i_synchro_state == SYNCHRO_REINIT )
    { 
       /*realign on video stream*/
       if( __AVI_ReAlign( p_input, p_info_video ) != 0 )
        {
            return( 0 ); /* assume EOF */
        }
        __AVI_SynchroReInit( p_input, p_info_video, p_info_audio );
    }

     /* update i_date if previously unselected ES (ex: 2 channels audio ) */
    if( (p_info_audio != NULL)&&(p_info_audio->b_unselected ))
    {
        /* we have to go to the good pts */
        /* we will reach p_info_ok pts */
        while( __AVI_GetPTS( p_info_audio) < __AVI_GetPTS( p_info_video) )
        {
            if( __AVI_NextIndexEntry( p_input, p_info_audio ) != 0 )
            {
                break;
            }
        }
       p_info_audio->b_unselected = 0 ;
    }

    /* what stream we should read in first */
    if( p_info_audio == NULL )
    {
        p_info = p_info_video;
    }
    else
    {
        if( __AVI_GetPTS( p_info_audio ) <= 
                        __AVI_GetPTS( p_info_video ) )
        {
            p_info = p_info_audio;
        }
        else
        {
            p_info = p_info_video;
        }
    }

    /* go to the good chunk to read */

    __AVI_SeekToChunk( p_input, p_info );
    
    /* now we just need to read a chunk */
    if( (p_chunk = RIFF_ReadChunk( p_input )) == NULL )
    {   
        intf_ErrMsg( "input demux: cannot read chunk" );
        return( -1 );
    }

    if( (p_chunk->i_id&0xFFFF0000) != 
                    (p_info->p_index[p_info->i_idxpos].i_id&0xFFFF0000) )
    {
        intf_WarnMsg( 2, "input demux: bad index entry" );
        __AVI_NextIndexEntry( p_input, p_info );
        return( 1 );
    }
/*    
    intf_WarnMsg( 6, "input demux: read %4.4s chunk %d bytes",
                    (char*)&p_chunk->i_id,
                    p_chunk->i_size);
*/                    
    if( RIFF_LoadChunkDataInPES(p_input, p_chunk, &p_pes) != 0 )
    {
        intf_ErrMsg( "input error: cannot read data" );
        return( -1 );
    }
/* i_rate is about how fast we read it (slow, fast ...) */
/* TODO: handle i_rate */
    vlc_mutex_lock( &p_input->stream.stream_lock );
    p_pes->i_rate =  p_input->stream.control.i_rate;
    vlc_mutex_unlock( &p_input->stream.stream_lock );

    p_pes->i_pts = p_avi_demux->i_date + __AVI_GetPTS( p_info );
    p_pes->i_dts = 0;
    
    /* send to decoder */
    vlc_mutex_lock( &p_info->p_es->p_decoder_fifo->data_lock );
    /* change MAX_PACKET and replace it to have same duration of audio 
        and video in buffer, to avoid to much unsynchronization while seeking */
    if( p_info->p_es->p_decoder_fifo->i_depth >= MAX_PACKETS_IN_FIFO )
    {
        /* Wait for the decoder. */
        vlc_cond_wait( &p_info->p_es->p_decoder_fifo->data_wait, 
                        &p_info->p_es->p_decoder_fifo->data_lock );
    }
    vlc_mutex_unlock( &p_info->p_es->p_decoder_fifo->data_lock );
    input_DecodePES( p_info->p_es->p_decoder_fifo, p_pes );

    if( p_info == p_info_video )
    {
        if( __AVI_NextIndexEntry( p_input, p_info ) != 0 )
        {
            return( 0 );
        }
    }
    else
    {
        __AVI_NextIndexEntry( p_input, p_info );
    }
         
    return( 1 );
}
