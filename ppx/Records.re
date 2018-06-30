open Migrate_parsetree;
open Ast_402;
open Ppx_tools_402;
open Parsetree;
open Ast_helper;
open Longident;
open Utils;

type parsedDecl = {
    name: string,
    str: expression,
    field: expression,
    codecs: (expression, expression)
};

let generateEncoder = (decls) => {
    let arrExpr = decls
        |> List.map(({ str, field, codecs: (encoder, _) }) =>
            [%expr ([%e str], [%e encoder]([%e field]))]
        )
        |> Ast_helper.Exp.array;

    [%expr [%e arrExpr] |> Js.Dict.fromArray |> Js.Json.object_]
        |> Exp.fun_("", None, [%pat? v]);
};

let generateDictGet = ({ str, codecs: (_, decoder) }) => {
    [%expr
        switch (Js.Dict.get(dict, [%e str])) {
            | None => Decco.error("Key not found", v)
            | Some(json) => [%e decoder](json)
        }
    ];
};

let generateDictGets = (decls) => decls
    |> List.map(generateDictGet)
    |> tupleOrSingleton(Exp.tuple);

let generateErrorCase = (numDecls, i, { str }) => {
    pc_lhs:
        Array.init(numDecls, which =>
            which === i ? [%pat? Error(e)] : [%pat? _]
        )
        |> Array.to_list
        |> tupleOrSingleton(Pat.tuple),
    pc_guard: None,
    pc_rhs: [%expr Error({ ...e, path: "." ++ [%e str] ++ e.path })]
};

let generateSuccessCase = (decls) => {
    pc_lhs: decls
        |> List.map(({ name }) =>
            Location.mknoloc(name)
                |> Pat.var
                |> (p) => Pat.construct(Ast_convenience.lid("Ok"), Some(p))
        )
        |> tupleOrSingleton(Pat.tuple),
    pc_guard: None,
    pc_rhs: decls
        |> List.map(({ name }) => (Ast_convenience.lid(name), makeIdentExpr(name)))
        |> (l) => Some(Exp.record(l, None))
        |> Exp.construct(Ast_convenience.lid("Ok"))

};

let generateDecoder = (decls) => {
    let resultSwitch = List.mapi(generateErrorCase(List.length(decls)), decls)
        |> List.append([generateSuccessCase(decls)])
        |> Ast_helper.Exp.match(generateDictGets(decls));

    [%expr (v) =>
        switch (Js.Json.classify(v)) {
            | Js.Json.JSONObject(dict) => [%e resultSwitch]
            | _ => Decco.error("Not an object", v)
        }
    ]
};

let parseDecl = ({ pld_name: { txt }, pld_loc, pld_type: { ptyp_desc } }) => {
    name: txt,
    str: Exp.constant(Asttypes.Const_string(txt, None)),
    field: Ast_convenience.lid(txt)
        |> Ast_helper.Exp.field([%expr v]),
    codecs: Codecs.generateCodecs(ptyp_desc, pld_loc)
};

let generateCodecs = (decls) => {
    let parsedDecls = List.map(parseDecl, decls);
    (generateEncoder(parsedDecls), generateDecoder(parsedDecls))
};
