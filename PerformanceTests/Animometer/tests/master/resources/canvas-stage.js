SimpleCanvasStage = Utilities.createSubclass(Stage,
    function(canvasObject)
    {
        Stage.call(this);
        this._canvasObject = canvasObject;
        this.objects = [];
    }, {

    initialize: function(benchmark)
    {
        Stage.prototype.initialize.call(this, benchmark);
        this.context = this.element.getContext("2d");
    },

    tune: function(count)
    {
        if (count == 0)
            return this.objects.length;

        if (count > 0) {
            for (var i = 0; i < count; ++i)
                this.objects.push(new this._canvasObject(this));
            return this.objects.length;
        }

        count = Math.min(-count, this.objects.length);
        this.objects.splice(0, count);
        return this.objects.length;
    },

    animate: function()
    {
        var context = this.context;
        context.clearRect(0, 0, this.size.x, this.size.y);
        this.objects.forEach(function(object) {
            object.draw(context);
        });
    },

    complexity: function()
    {
        return this.objects.length;
    }
});
