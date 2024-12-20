import avro.schema
from avro.datafile import DataFileReader, DataFileWriter
from avro.io import DatumReader, DatumWriter



json_schema = """
{"namespace": "example.avro",
 "type": "record",
 "name": "User",
 "fields": [
     {"name": "name", "type": "string"},
     {"name": "favorite_number",  "type": ["int", "null"]},
     {"name": "favorite_color", "type": ["string", "null"]}
 ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("users.avro", "wb"), DatumWriter(), schema)
writer.append({"name": "Alyssa", "favorite_number": 256})
writer.append({"name": "Ben", "favorite_number": 7, "favorite_color": "red"})
writer.close()

reader = DataFileReader(open("users.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()




json_schema = """
{"namespace": "example2.avro",
 "type": "int",
 "name": "my_int"
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("root-int.avro", "wb"), DatumWriter(), schema)
writer.append(42)
writer.append(43)

writer.close()

reader = DataFileReader(open("root-int.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()



json_schema = """
{ "type": "record",
 "name": "root",
 "fields": [
     {"name": "single_union", "type": ["int"]}
 ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("single-union.avro", "wb"), DatumWriter(), schema)
writer.append({ "single_union":42})


writer.close()

reader = DataFileReader(open("single-union.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()




json_schema = """
{ "type": "record",
 "name": "root",
 "fields": [
     {"name": "null_first", "type": ["null","int"]}
 ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("null_first.avro", "wb"), DatumWriter(), schema)
writer.append({ "null_first":42})
writer.append({})


writer.close()

reader = DataFileReader(open("null_first.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()




json_schema = """
{ "type": "record",
 "name": "root",
 "fields": [
     {"name": "null_last", "type": ["int","null"]}
 ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("null_last.avro", "wb"), DatumWriter(), schema)
writer.append({ "null_last":42})
writer.append({})


writer.close()

reader = DataFileReader(open("null_last.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()


json_schema = """
{ "type": "record",
 "name": "root",
 "fields": [
     {"name": "null", "type": "null"},
     {"name": "boolean", "type": "boolean"},
     {"name": "int", "type": "int"},
     {"name": "long", "type": "long"},
     {"name": "float", "type": "float"},
     {"name": "double", "type": "double"},
     {"name": "bytes", "type": "bytes"},
     {"name": "string", "type": "string"}
 ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("primitive_types.avro", "wb"), DatumWriter(), schema)



writer.append({ 'null':None, 'boolean': False, 'int': -2147483648, 'long' : -9223372036854775808, 'float' : -3.4028235e+38, 'double' : -1.7976931348623157e+308,  'bytes' : 'thisisalongblob\x00withnullbytes'.encode(),  'string' : "🦆🦆🦆🦆🦆🦆"})
writer.append({ 'null':None, 'boolean': True, 'int': 2147483647, 'long' : 9223372036854775807,  'float' : 3.4028235e+38, 'double' : 1.7976931348623157e+308, 'bytes': '\x00\x00\x00a'.encode(),  'string' : 'goo'})


writer.close()

reader = DataFileReader(open("primitive_types.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()



json_schema = """
{
    "type": "record",
    "name": "MySchema",
    "namespace": "com.company",
    "fields": [
        {
            "name": "color",
            "type": {
                "type": "enum",
                "name": "Color",
                "symbols": [
                    "UNKNOWN",
                    "GREEN",
                    "RED"
                ]
            },
            "default": "UNKNOWN"
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("enum.avro", "wb"), DatumWriter(), schema)



writer.append({ 'color': 'GREEN'})
writer.append({ 'color': 'GREEN'})
writer.append({ 'color': 'RED'})
writer.append({ 'color': 'UNKNOWN'})
writer.append({ 'color': 'UNKNOWN'})

writer.close()

reader = DataFileReader(open("enum.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()







json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "md5",
            "type": {"type": "fixed", "size": 32, "name": "md5"}
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("fixed.avro", "wb"), DatumWriter(), schema)



writer.append({ 'md5' : '47336f3f2497b70ac046cf23298e20a7'.encode()})
writer.append({ 'md5' : 'a789a15a7ff7db4a0d1b186363ef0771'.encode()})
writer.append({ 'md5' : 'c9db7c67a6acb5a65c78b19e9e01d7b0'.encode()})
writer.append({ 'md5' : 'ac441296bcbd44442301204a8f061cf2'.encode()})



writer.close()

reader = DataFileReader(open("fixed.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()









json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "string_arr",
            "type": {
              "type": "array",
              "items" : "string",
              "default": []
            }
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("string_array.avro", "wb"), DatumWriter(), schema)



writer.append({ 'string_arr' : ['Hello' ,'World']})
writer.append({ 'string_arr' : ['this']})
writer.append({ 'string_arr' : []})
writer.append({ 'string_arr' : ['is', 'cool','array']})
writer.append({ 'string_arr' : ['data']})



writer.close()

reader = DataFileReader(open("string_array.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()




json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "long_map",
            "type":  {
               "type": "map",
               "values" : "long",
               "default": {}
             }
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("long_map.avro", "wb"), DatumWriter(), schema)

writer.append({ 'long_map' : {'one': 42}})
writer.append({ 'long_map' : {'two': 43}})
writer.append({ 'long_map' : {'three': 44}})

writer.close()

reader = DataFileReader(open("long_map.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()



json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "string_arr",
            "type": ["null", {
              "type": "array",
              "items" : "string",
              "default": []
            }]
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("nullable_string_array.avro", "wb"), DatumWriter(), schema)



writer.append({ 'string_arr' : ['Hello' ,'World']})
writer.append({ 'string_arr' : ['this']})
writer.append({ 'string_arr' : []})
writer.append({ 'string_arr' : None})
writer.append({ 'string_arr' : None})
writer.append({ 'string_arr' : ['is', 'cool','array']})
writer.append({ 'string_arr' : ['data']})



writer.close()

reader = DataFileReader(open("nullable_string_array.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()





json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "string_arr",
            "type": {
              "type": "array",
              "items" : ["string", "null"],
              "default": []
            }
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("nullable_entry_string_array.avro", "wb"), DatumWriter(), schema)



writer.append({ 'string_arr' : ['Hello' ,None, 'World']})
writer.append({ 'string_arr' : ['this']})
writer.append({ 'string_arr' : [None]})
writer.append({ 'string_arr' : [None, None, None]})
writer.append({ 'string_arr' : []})
writer.append({ 'string_arr' : [None, 'is', 'cool',None, 'array',None]})
writer.append({ 'string_arr' : ['data',None]})



writer.close()

reader = DataFileReader(open("nullable_entry_string_array.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()





json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "string_arr",
            "type": ["null", {
              "type": "array",
              "items" : ["string", "null"],
              "default": []
            }]
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("all_nullable_list.avro", "wb"), DatumWriter(), schema)



writer.append({ 'string_arr' : ['Hello' ,None, 'World']})
writer.append({ 'string_arr' : ['this']})
writer.append({ 'string_arr' : [None]})
writer.append({ 'string_arr' : [None, None, None]})
writer.append({ 'string_arr' : []})
writer.append({ 'string_arr' : None})
writer.append({ 'string_arr' : None})
writer.append({ 'string_arr' : [None, 'is', 'cool',None, 'array',None]})
writer.append({ 'string_arr' : ['data',None]})



writer.close()

reader = DataFileReader(open("all_nullable_list.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()




json_schema = """
{ "type": "record",
 "name": "root",
     "fields": [
        {
            "name": "nested_ints",
            "type": ["null", {
              "type": "array",
              "items" : ["null", {
                  "type": "array",
                  "items" : ["int", "null"],
                  "default": []
                }],
              "default": []
            }]
        }
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("nested_nullable_lists.avro", "wb"), DatumWriter(), schema)


writer.append({ 'nested_ints' : None})
writer.append({ 'nested_ints' : [None]})
writer.append({ 'nested_ints' : [[None], [None]]})
writer.append({ 'nested_ints' : [None, None]})
writer.append({ 'nested_ints' : [[42]]})
writer.append({ 'nested_ints' : [[42], [43]]})
writer.append({ 'nested_ints' : [[42, 43]]})
writer.append({ 'nested_ints' : [[42, 43], None, [44, 45]]})
writer.append({ 'nested_ints' : [[42, None, 43, None], None, [44, None, 45, None], None, [46]]})

writer.close()

reader = DataFileReader(open("nested_nullable_lists.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()









json_schema = """
{
  "type": "record",
  "name": "LongList",
  "fields" : [
    {"name": "value", "type": "long"},          
    {"name": "next", "type": ["null", "LongList"]}
  ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("recursive.avro", "wb"), DatumWriter(), schema)


writer.append({ 'value': 42})
writer.append({ 'value': 43, 'next' : {'value': 44}})
writer.append({ 'value': 43, 'next' : {'value': 44, 'next' : {'value': 45}}})

writer.close()

reader = DataFileReader(open("recursive.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()







json_schema = """
{ "type": "record",
"name": "root",
     "fields": [
    {"name": "n", "type": "null"}          
    ]
}
"""

schema = avro.schema.parse(json_schema)

writer = DataFileWriter(open("broken_record.avro", "wb"), DatumWriter(), schema)

writer.append({})
writer.append({})

# writer.append({ 'value': 42})
# writer.append({ 'value': 43, 'next' : {'value': 44}})
# writer.append({ 'value': 43, 'next' : {'value': 44, 'next' : {'value': 45}}})

writer.close()

reader = DataFileReader(open("broken_record.avro", "rb"), DatumReader())
for user in reader:
    print(user)
reader.close()






# record
# detect recursive types or what happens here?



