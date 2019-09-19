
#include <steem/chain/database.hpp>
#include <steem/chain/index.hpp>
#include <steem/chain/statefile/statefile.hpp>

#include <iostream>
#include <sstream>

namespace steem { namespace chain { namespace statefile {

void init_genesis_from_state( database& db, const std::string& state_filename )
{
   std::ifstream input_stream( state_filename, std::ios::binary );

   std::stringbuf top_header_buf;
   input_stream.get( top_header_buf );
   state_header top_header = fc::json::from_string( top_header_buf.str() ).as< state_header >();
   steem_version_info expected_version = steem_version_info( db );

   FC_ASSERT( top_header.version.db_format_version == expected_version.db_format_version, "DB Format Version mismatch" );
   FC_ASSERT( top_header.version.network_type == expected_version.network_type, "Network Type mismatch" );
   FC_ASSERT( top_header.version.chain_id == expected_version.chain_id, "Chain ID mismatch" );

   db.set_revision( top_header.version.head_block_num );

   flat_map< std::string, std::shared_ptr< index_info > > index_map;
   db.for_each_index_extension< index_info >( [&]( std::shared_ptr< index_info > info )
   {
      std::string name;
      info->get_schema()->get_name( name );
      index_map[ name ] = info;
   });

   flat_map< std::string, section_header > header_map;
   for( const auto& header : top_header.sections )
   {
      object_section obj_header = header.get< object_section >();
      header_map.insert_or_assign( obj_header.object_type, std::move( obj_header ) );
   }

   for( const auto& idx : index_map )
   {
      auto itr = header_map.find( idx.first );
      FC_ASSERT( itr != header_map.end(), "Did not find expected object index: ${o}", ("o", idx.first) );

      std::string expected_schema;
      idx.second->get_schema()->get_str_schema( expected_schema );

      FC_TODO( "Version object data to allow for upgrading during load" );
      FC_ASSERT( expected_schema == itr->second.get< object_section >().schema,
         "Unexpected incoming schema for object ${o}.\nExpected: ${e}\nActual: ${a}",
         ("o", idx.first)("e", expected_schema)("a", itr->second.get< object_section >().schema) );
   }

   input_stream.seekg( -sizeof( int64_t ), std::ios::end );
   int64_t footer_pos;
   input_stream.read( (char*)&footer_pos, sizeof( int64_t ) );

   input_stream.seekg( footer_pos );
   std::stringbuf footer_buf;
   input_stream.get( footer_buf );

   state_footer top_footer = fc::json::from_string( footer_buf.str() ).as< state_footer >();
   flat_map< std::string, section_footer > footer_map;

   for( int64_t i = 0; i < top_footer.section_footers.size(); i++ )
   {
      std::string& name = top_header.sections[ i ].get< object_section >().object_type;
      footer_map[ name ] = top_footer.section_footers[i];
   }

   for( const auto& h : header_map )
   {
      vector< char > section_header_bin;
      char c;

      input_stream.seekg( footer_map[ h.first ].begin_offset );
      int64_t begin = input_stream.tellg();

      std::stringbuf header_buf;
      input_stream.get( header_buf );
      object_section header = h.second.get< object_section >();
      object_section s_header = fc::json::from_string( header_buf.str() ).as< section_header >().get< object_section >();
      input_stream.read( &c, 1 );

      FC_ASSERT( header.object_type == s_header.object_type, "Expected next object type: ${e} actual: ${a}",
         ("e", header.object_type)("a", s_header.object_type) );
      FC_ASSERT( header.format == s_header.format, "Mismatched object format for ${o}.",
         ("o", header.object_type) );
      FC_ASSERT( header.object_count == s_header.object_count, "Mismatched object count for ${o}.",
         ("o", header.object_type) );
      FC_ASSERT( header.schema == s_header.schema, "Mismatched object schema for ${o}.",
         ("o", header.object_type) );

      ilog( "Unpacking ${o}. (${n} Objects)", ("o", header.object_type)("n", header.object_count) );
      std::shared_ptr< index_info > idx = index_map[ header.object_type ];

      for( int64_t i = 0; i < header.object_count; i++ )
      {
         if( header.format == FORMAT_BINARY )
         {
            idx->create_object_from_binary( db, input_stream );
         }
         else if( header.format == FORMAT_JSON )
         {
            std::stringbuf object_stream;
            input_stream.get( object_stream );
            idx->create_object_from_json( db, object_stream.str() );
            input_stream.read( &c, 1 );
         }
      }

      idx->set_next_id( db, header.next_id );

      int64_t end = input_stream.tellg();

      std::stringbuf footer_buf;
      input_stream.get( footer_buf );
      section_footer s_footer = fc::json::from_string( footer_buf.str() ).as< section_footer >();

      FC_ASSERT( s_footer.begin_offset == begin, "Begin offset mismatch for ${o}",
         ("o", header.object_type) );
      FC_ASSERT( s_footer.end_offset == end, "Begin offset mismatch for ${o}",
         ("o", header.object_type) );
      FC_TODO( "Compare hashes" );
   }
}

} } } // steem::chain::statefile