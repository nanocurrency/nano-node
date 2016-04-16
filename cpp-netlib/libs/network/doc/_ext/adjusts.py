from sphinx.writers import html as sphinx_htmlwriter

class CppNetlibHTMLTranslator(sphinx_htmlwriter.SmartyPantsHTMLTranslator):
    """
    cpp-netlib-customized HTML transformations of documentation. Based on
    djangodocs.DjangoHTMLTranslator
    """

    def visit_section(self, node):
        node['ids'] = map(lambda x: "cpp-netlib-%s" % x, node['ids'])
        sphinx_htmlwriter.SmartyPantsHTMLTranslator.visit_section(self, node)
